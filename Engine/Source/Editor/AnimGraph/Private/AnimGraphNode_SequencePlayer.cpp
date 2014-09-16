// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphPrivatePCH.h"
#include "AnimGraphNode_SequencePlayer.h"

#include "CompilerResultsLog.h"
#include "GraphEditorActions.h"
#include "AssetRegistryModule.h"
#include "AnimationGraphSchema.h"
#define LOCTEXT_NAMESPACE "A3Nodes"

/////////////////////////////////////////////////////
// FNewSequencePlayerAction

// Action to add a sequence player node to the graph
struct FNewSequencePlayerAction : public FEdGraphSchemaAction_K2NewNode
{
protected:
	FAssetData AssetInfo;
public:
	FNewSequencePlayerAction(const FAssetData& InAssetInfo, FText Title)
	{
		AssetInfo = InAssetInfo;

		UAnimGraphNode_SequencePlayer* Template = NewObject<UAnimGraphNode_SequencePlayer>();
		NodeTemplate = Template;

		MenuDescription = Title;
		TooltipDescription = TEXT("Evaluates an animation sequence to produce a pose");
		Category = TEXT("Animations");

		// Grab extra keywords
		Keywords = InAssetInfo.ObjectPath.ToString();
	}

	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
	{
		UAnimGraphNode_SequencePlayer* SpawnedNode = CastChecked<UAnimGraphNode_SequencePlayer>(FEdGraphSchemaAction_K2NewNode::PerformAction(ParentGraph, FromPin, Location, bSelectNewNode));
		SpawnedNode->Node.Sequence = Cast<UAnimSequence>(AssetInfo.GetAsset());

		return SpawnedNode;
	}
};

/////////////////////////////////////////////////////
// UAnimGraphNode_SequencePlayer

UAnimGraphNode_SequencePlayer::UAnimGraphNode_SequencePlayer(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
}

void UAnimGraphNode_SequencePlayer::PreloadRequiredAssets()
{
	PreloadObject(Node.Sequence);

	Super::PreloadRequiredAssets();
}

FLinearColor UAnimGraphNode_SequencePlayer::GetNodeTitleColor() const
{
	if ((Node.Sequence != NULL) && Node.Sequence->IsValidAdditive())
	{
		return FLinearColor(0.10f, 0.60f, 0.12f);
	}
	else
	{
		return FColor(200, 100, 100);
	}
}

FText UAnimGraphNode_SequencePlayer::GetTooltipText() const
{
	const bool bAdditive = ((Node.Sequence != NULL) && Node.Sequence->IsValidAdditive());
	return GetTitleGivenAssetInfo(FText::FromString(Node.Sequence->GetPathName()), bAdditive);
}

FText UAnimGraphNode_SequencePlayer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (Node.Sequence == nullptr)
	{
		return LOCTEXT("SequenceNullTitle", "Play (None)");
	}
	else if (!CachedNodeTitles.IsTitleCached(TitleType))
	{
		if(SyncGroup.GroupName == NAME_None)
		{
			TitleType = ENodeTitleType::ListView;

			if(TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
			{
				const bool bAdditive = Node.Sequence->IsValidAdditive();
				// FText::Format() is slow, so we cache this to save on performance
				CachedNodeTitles.SetCachedTitle(TitleType, GetTitleGivenAssetInfo(FText::FromName(Node.Sequence->GetFName()), bAdditive));
			}
		}
		else if (TitleType == ENodeTitleType::FullTitle)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Title"), CachedNodeTitles[ENodeTitleType::ListView]);
			Args.Add(TEXT("SyncGroup"), FText::FromName(SyncGroup.GroupName));

			// FText::Format() is slow, so we cache this to save on performance
			CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("SequenceNodeGroupWithSubtitle", "{Title}\nSync group {SyncGroup}"), Args));
		}
	}
	return CachedNodeTitles[TitleType];
}

FText UAnimGraphNode_SequencePlayer::GetTitleGivenAssetInfo(const FText& AssetName, bool bKnownToBeAdditive)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), AssetName);

	if (bKnownToBeAdditive)
	{
		return FText::Format(LOCTEXT("SequenceNodeTitleAdditive", "Play {AssetName} (additive)"), Args);
	}
	else
	{
		return FText::Format(LOCTEXT("SequenceNodeTitle", "Play {AssetName}"), Args);
	}
}

void UAnimGraphNode_SequencePlayer::GetMenuEntries(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	if ((ContextMenuBuilder.FromPin == NULL) || (UAnimationGraphSchema::IsPosePin(ContextMenuBuilder.FromPin->PinType) && (ContextMenuBuilder.FromPin->Direction == EGPD_Input)))
	{
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked(ContextMenuBuilder.CurrentGraph);

		if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
		{
			// Load the asset registry module
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			FARFilter Filter;
			Filter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
			Filter.bRecursiveClasses = true;

			// Filter by skeleton
			FAssetData SkeletonData(AnimBlueprint->TargetSkeleton);
			Filter.TagsAndValues.Add(TEXT("Skeleton"), SkeletonData.GetExportTextName());

			// Find matching assets and add an entry for each one
			TArray<FAssetData> SequenceList;
			AssetRegistryModule.Get().GetAssets(Filter, /*out*/ SequenceList);

			for (auto AssetIt = SequenceList.CreateConstIterator(); AssetIt; ++AssetIt)
			{
				const FAssetData& Asset = *AssetIt;

				// Try to determine if the asset is additive (can't do it right now if the asset is unloaded)
				bool bAdditive = false;
				if (Asset.IsAssetLoaded())
				{
					if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset.GetAsset()))
					{
						bAdditive = Sequence->IsValidAdditive();
					}
				}

				// Create the menu item
				const FText Title = UAnimGraphNode_SequencePlayer::GetTitleGivenAssetInfo(FText::FromName(Asset.AssetName), bAdditive);
				TSharedPtr<FNewSequencePlayerAction> NewAction(new FNewSequencePlayerAction(Asset, Title));
				ContextMenuBuilder.AddAction( NewAction );
			}
		}
	}
}

void UAnimGraphNode_SequencePlayer::ValidateAnimNodeDuringCompilation(class USkeleton* ForSkeleton, class FCompilerResultsLog& MessageLog)
{
	if (Node.Sequence == NULL)
	{
		MessageLog.Error(TEXT("@@ references an unknown sequence"), this);
	}
	else
	{
		USkeleton * SeqSkeleton = Node.Sequence->GetSkeleton();
		if (SeqSkeleton&& // if anim sequence doesn't have skeleton, it might be due to anim sequence not loaded yet, @todo: wait with anim blueprint compilation until all assets are loaded?
			!SeqSkeleton->IsCompatible(ForSkeleton))
		{
			MessageLog.Error(TEXT("@@ references sequence that uses different skeleton @@"), this, SeqSkeleton);
		}
	}
}

void UAnimGraphNode_SequencePlayer::GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const
{
	if (!Context.bIsDebugging)
	{
		// add an option to convert to single frame
		Context.MenuBuilder->BeginSection("AnimGraphNodeSequencePlayer", NSLOCTEXT("A3Nodes", "SequencePlayerHeading", "Sequence Player"));
		{
			Context.MenuBuilder->AddMenuEntry(FGraphEditorCommands::Get().OpenRelatedAsset);
			Context.MenuBuilder->AddMenuEntry(FGraphEditorCommands::Get().ConvertToSeqEvaluator);
		}
		Context.MenuBuilder->EndSection();
	}
}

void UAnimGraphNode_SequencePlayer::BakeDataDuringCompilation(class FCompilerResultsLog& MessageLog)
{
	UAnimBlueprint* AnimBlueprint = GetAnimBlueprint();
	Node.GroupIndex = AnimBlueprint->FindOrAddGroup(SyncGroup.GroupName);
	Node.GroupRole = SyncGroup.GroupRole;
}

void UAnimGraphNode_SequencePlayer::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& ComplexAnims, TArray<UAnimSequence*>& AnimationSequences) const
{
	if(Node.Sequence)
	{
		HandleAnimReferenceCollection(Node.Sequence, ComplexAnims, AnimationSequences);
	}
}

void UAnimGraphNode_SequencePlayer::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& ComplexAnimsMap, const TMap<UAnimSequence*, UAnimSequence*>& AnimSequenceMap)
{
	HandleAnimReferenceReplacement(Node.Sequence, ComplexAnimsMap, AnimSequenceMap);
}


bool UAnimGraphNode_SequencePlayer::DoesSupportTimeForTransitionGetter() const
{
	return true;
}

UAnimationAsset* UAnimGraphNode_SequencePlayer::GetAnimationAsset() const 
{
	return Node.Sequence;
}

const TCHAR* UAnimGraphNode_SequencePlayer::GetTimePropertyName() const 
{
	return TEXT("InternalTimeAccumulator");
}

UScriptStruct* UAnimGraphNode_SequencePlayer::GetTimePropertyStruct() const 
{
	return FAnimNode_SequencePlayer::StaticStruct();
}

#undef LOCTEXT_NAMESPACE
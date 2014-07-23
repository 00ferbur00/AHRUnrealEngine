// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "BehaviorTreeEditorPrivatePCH.h"
#include "Toolkits/AssetEditorManager.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTreeEditorModule.h"
#include "BehaviorTreeEditor.h"

#include "AssetTypeActions_BehaviorTree.h"
#include "SBehaviorTreeDiff.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

bool FAssetTypeActions_BehaviorTree::HasActions ( const TArray<UObject*>& InObjects ) const
{
	return true;
}

void FAssetTypeActions_BehaviorTree::GetActions( const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder )
{
	auto BehaviorTrees = GetTypedWeakObjectPtrs<UBehaviorTree>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("BehaviorTree_Edit", "Edit"),
		LOCTEXT("BehaviorTree_EditTooltip", "Opens the selected Behavior Tree in editor."),
		FSlateIcon(),
		FUIAction(
		FExecuteAction::CreateSP( this, &FAssetTypeActions_BehaviorTree::ExecuteEdit, BehaviorTrees ),
		FCanExecuteAction()
		)
	);
}

void FAssetTypeActions_BehaviorTree::OpenAssetEditor( const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor )
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for(auto Object : InObjects)
	{
		auto BehaviorTree = Cast<UBehaviorTree>(Object);
		if(BehaviorTree != nullptr)
		{
			// check if we have an editor open for this BT's blackboard & use that if we can
			bool bFoundExisting = false;
			if(BehaviorTree->BlackboardAsset != nullptr)
			{
				const bool bFocusIfOpen = false;
				FBehaviorTreeEditor* ExistingInstance = static_cast<FBehaviorTreeEditor*>(FAssetEditorManager::Get().FindEditorForAsset(BehaviorTree->BlackboardAsset, bFocusIfOpen));
				if(ExistingInstance != nullptr && ExistingInstance->GetBehaviorTree() == nullptr)
				{
					ExistingInstance->InitBehaviorTreeEditor(Mode, EditWithinLevelEditor, BehaviorTree);
					bFoundExisting = true;
				}
			}
			
			if(!bFoundExisting)
			{
				FBehaviorTreeEditorModule& BehaviorTreeEditorModule = FModuleManager::GetModuleChecked<FBehaviorTreeEditorModule>( "BehaviorTreeEditor" );
				TSharedRef< IBehaviorTreeEditor > NewEditor = BehaviorTreeEditorModule.CreateBehaviorTreeEditor( Mode, EditWithinLevelEditor, BehaviorTree );	
			}
		}
	}
}

void FAssetTypeActions_BehaviorTree::ExecuteEdit(TArray<TWeakObjectPtr<UBehaviorTree>> Objects)
{
	for(auto Object : Objects)
	{
		auto BehaviorTree = Object.Get();
		if(BehaviorTree != NULL)
		{
			FAssetEditorManager::Get().OpenEditorForAsset(BehaviorTree);
		}
	}
}

UClass* FAssetTypeActions_BehaviorTree::GetSupportedClass() const
{ 
	return UBehaviorTree::StaticClass(); 
}

void FAssetTypeActions_BehaviorTree::PerformAssetDiff(UObject* OldAsset, UObject* NewAsset, const struct FRevisionInfo& OldRevision, const struct FRevisionInfo& NewRevision) const
{
	UBehaviorTree* OldBehaviorTree = Cast<UBehaviorTree>(OldAsset);
	check(OldBehaviorTree != NULL);

	UBehaviorTree* NewBehaviorTree = Cast<UBehaviorTree>(NewAsset);
	check(NewBehaviorTree != NULL);

	// sometimes we're comparing different revisions of one single asset (other 
	// times we're comparing two completely separate assets altogether)
	bool bIsSingleAsset = (NewBehaviorTree->GetName() == OldBehaviorTree->GetName());

	FText WindowTitle = LOCTEXT("NamelessBehaviorTreeDiff", "Behavior Tree Diff");
	// if we're diff'ing one asset against itself 
	if (bIsSingleAsset)
	{
		// identify the assumed single asset in the window's title
		WindowTitle = FText::Format(LOCTEXT("Behavior Tree Diff", "{0} - Behavior Tree Diff"), FText::FromString(NewBehaviorTree->GetName()));
	}

	const TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(1000,800));

	Window->SetContent(SNew(SBehaviorTreeDiff)
		.BehaviorTreeOld(OldBehaviorTree)
		.BehaviorTreeNew(NewBehaviorTree)
		.OldRevision(OldRevision)
		.NewRevision(NewRevision)
		.ShowAssetNames(!bIsSingleAsset)
		.OpenInDefaults(const_cast<FAssetTypeActions_BehaviorTree*>(this), &FAssetTypeActions_BehaviorTree::OpenInDefaults) );

	// Make this window a child of the modal window if we've been spawned while one is active.
	TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if ( ActiveModal.IsValid() )
	{
		FSlateApplication::Get().AddWindowAsNativeChild( Window.ToSharedRef(), ActiveModal.ToSharedRef() );
	}
	else
	{
		FSlateApplication::Get().AddWindow( Window.ToSharedRef() );
	}
}

void FAssetTypeActions_BehaviorTree::OpenInDefaults( class UBehaviorTree* OldBehaviorTree, class UBehaviorTree* NewBehaviorTree ) const
{
	FString OldTextFilename = DumpAssetToTempFile(OldBehaviorTree);
	FString NewTextFilename = DumpAssetToTempFile(NewBehaviorTree);

	// Get diff program to use
	FString DiffCommand = GetDefault<UEditorLoadingSavingSettings>()->TextDiffToolPath.FilePath;

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateDiffProcess(DiffCommand, OldTextFilename, NewTextFilename);
}

#undef LOCTEXT_NAMESPACE

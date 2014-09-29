// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "BlueprintGraphPrivatePCH.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "EdGraphSchema_K2.h"	// for FBlueprintMetadata
#include "GameFramework/Actor.h"
#include "BlueprintVariableNodeSpawner.h"
#include "BlueprintNodeTemplateCache.h" // for IsTemplateOuter()
#include "BlueprintActionFilter.h" // for FBlueprintActionContext
#include "EditorCategoryUtils.h" // for BuildCategoryString()

#define LOCTEXT_NAMESPACE "BlueprintFunctionNodeSpawner"

/*******************************************************************************
 * Static UBlueprintFunctionNodeSpawner Helpers
 ******************************************************************************/

//------------------------------------------------------------------------------
namespace BlueprintFunctionNodeSpawnerImpl
{
	FVector2D BindingOffset = FVector2D::ZeroVector;
	static const FText FallbackCategory = LOCTEXT("UncategorizedFallbackCategory", "Call Function");

	/**
	 * 
	 * 
	 * @param  NewNode	
	 * @param  BoundObject	
	 * @return 
	 */
	static bool BindFunctionNode(UK2Node_CallFunction* NewNode, UObject* BoundObject);

	/**
	 * 
	 * 
	 * @param  NewNode	
	 * @param  BindingSpawner	
	 * @return 
	 */
	template <class NodeType>
	static bool BindFunctionNode(UK2Node_CallFunction* NewNode, UBlueprintNodeSpawner* BindingSpawner);

	/**
	 * 
	 * 
	 * @param  InputNode
	 * @return 
	 */
	static FVector2D CalculateBindingPosition(UEdGraphNode* const InputNode);
}

//------------------------------------------------------------------------------
static bool BlueprintFunctionNodeSpawnerImpl::BindFunctionNode(UK2Node_CallFunction* NewNode, UObject* BoundObject)
{
	bool bSuccessfulBinding = false;

	bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(NewNode->GetGraph());
	if (!bIsTemplateNode)
	{
		if (UProperty const* BoundProperty = Cast<UProperty>(BoundObject))
		{
			if (UK2Node_CallFunctionOnMember* CallOnMemberNode = Cast<UK2Node_CallFunctionOnMember>(NewNode))
			{
				// force bIsConsideredSelfContext to false, else the target 
				// could end up being the skeleton class (and functionally, 
				// there is no difference)
				CallOnMemberNode->MemberVariableToCallOn.SetFromField<UProperty>(BoundProperty, /*bIsConsideredSelfContext =*/false);
				bSuccessfulBinding = true;
				CallOnMemberNode->ReconstructNode();
			}
			else
			{
				UBlueprintNodeSpawner* TempNodeSpawner = UBlueprintVariableNodeSpawner::Create(UK2Node_VariableGet::StaticClass(), BoundProperty);
				bSuccessfulBinding = BindFunctionNode<UK2Node_VariableGet>(NewNode, TempNodeSpawner);
			}
		}
		else if (AActor* BoundActor = Cast<AActor>(BoundObject))
		{
			auto PostSpawnSetupLambda = [](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/, AActor* ActorInst)
			{
				UK2Node_Literal* ActorRefNode = CastChecked<UK2Node_Literal>(NewNode);
				ActorRefNode->SetObjectRef(ActorInst);
			};
			UBlueprintNodeSpawner::FCustomizeNodeDelegate PostSpawnDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(PostSpawnSetupLambda, BoundActor);
			
			UBlueprintNodeSpawner* TempNodeSpawner = UBlueprintNodeSpawner::Create<UK2Node_Literal>(/*Outer =*/GetTransientPackage(), PostSpawnDelegate);
			bSuccessfulBinding = BindFunctionNode<UK2Node_Literal>(NewNode, TempNodeSpawner);
		}		
	}
	return bSuccessfulBinding;
}

//------------------------------------------------------------------------------
template <class NodeType>
static bool BlueprintFunctionNodeSpawnerImpl::BindFunctionNode(UK2Node_CallFunction* NewNode, UBlueprintNodeSpawner* BindingSpawner)
{
	bool bSuccessfulBinding = false;

	FVector2D BindingPos = CalculateBindingPosition(NewNode);
	UEdGraph* ParentGraph = NewNode->GetGraph();
	NodeType* BindingNode = CastChecked<NodeType>(BindingSpawner->Invoke(ParentGraph, IBlueprintNodeBinder::FBindingSet(), BindingPos));

	BindingOffset.Y += UEdGraphSchema_K2::EstimateNodeHeight(BindingNode);

	// @TODO: Move this down into Invoke()
	ParentGraph->Modify();
	ParentGraph->AddNode(BindingNode, /*bFromUI =*/false, /*bSelectNewNode =*/false);

	UEdGraphPin* LiteralOutput = BindingNode->GetValuePin();
	UEdGraphPin* CallSelfInput = NewNode->FindPin(GetDefault<UEdGraphSchema_K2>()->PN_Self);
	// connect the new "get-var" node with the spawned function node
	if ((LiteralOutput != nullptr) && (CallSelfInput != nullptr))
	{
		LiteralOutput->MakeLinkTo(CallSelfInput);
		bSuccessfulBinding = true;
	}

	return bSuccessfulBinding;
}

//------------------------------------------------------------------------------
static FVector2D BlueprintFunctionNodeSpawnerImpl::CalculateBindingPosition(UEdGraphNode* const InputNode)
{
	float const EstimatedVarNodeWidth = 224.0f;
	FVector2D AttachingNodePos;
	AttachingNodePos.X = InputNode->NodePosX - EstimatedVarNodeWidth;
	AttachingNodePos.Y = InputNode->NodePosY;

	float const EstimatedVarNodeHeight = 48.0f;
	float const EstimatedFuncNodeHeight = UEdGraphSchema_K2::EstimateNodeHeight(InputNode);
	float const FuncNodeMidYCoordinate = InputNode->NodePosY + (EstimatedFuncNodeHeight / 2.0f);
	AttachingNodePos.Y = FuncNodeMidYCoordinate - (EstimatedVarNodeWidth / 2.0f);

	AttachingNodePos += BindingOffset;
	return AttachingNodePos;
}

/*******************************************************************************
 * UBlueprintFunctionNodeSpawner
 ******************************************************************************/

//------------------------------------------------------------------------------
// Evolved from FK2ActionMenuBuilder::AddSpawnInfoForFunction()
UBlueprintFunctionNodeSpawner* UBlueprintFunctionNodeSpawner::Create(UFunction const* const Function, UObject* Outer/* = nullptr*/)
{
	check(Function != nullptr);

	bool const bIsPure = Function->HasAllFunctionFlags(FUNC_BlueprintPure);
	bool const bHasArrayPointerParms = Function->HasMetaData(TEXT("ArrayParm"));
	bool const bIsCommutativeAssociativeBinaryOp = Function->HasMetaData(FBlueprintMetadata::MD_CommutativeAssociativeBinaryOperator);
	bool const bIsMaterialParamCollectionFunc = Function->HasMetaData(FBlueprintMetadata::MD_MaterialParameterCollectionFunction);
	bool const bIsDataTableFunc = Function->HasMetaData(FBlueprintMetadata::MD_DataTablePin);

	TSubclassOf<UK2Node_CallFunction> NodeClass;
	if (bIsCommutativeAssociativeBinaryOp && bIsPure)
	{
		NodeClass = UK2Node_CommutativeAssociativeBinaryOperator::StaticClass();
	}
	else if (bIsMaterialParamCollectionFunc)
	{
		NodeClass = UK2Node_CallMaterialParameterCollectionFunction::StaticClass();
	}
	else if (bIsDataTableFunc)
	{
		NodeClass = UK2Node_CallDataTableFunction::StaticClass();
	}
	// @TODO: else if bIsParentContext => UK2Node_CallParentFunction
	else if (bHasArrayPointerParms)
	{
		NodeClass = UK2Node_CallArrayFunction::StaticClass();
	}
	else
	{
		NodeClass = UK2Node_CallFunction::StaticClass();
	}

	return Create(NodeClass, Function, Outer);
}

//------------------------------------------------------------------------------
UBlueprintFunctionNodeSpawner* UBlueprintFunctionNodeSpawner::Create(TSubclassOf<UK2Node_CallFunction> NodeClass, UFunction const* const Function, UObject* Outer/* = nullptr*/)
{
	if (Outer == nullptr)
	{
		Outer = GetTransientPackage();
	}

	//--------------------------------------
	// Constructing the Spawner
	//--------------------------------------

	UBlueprintFunctionNodeSpawner* NodeSpawner = NewObject<UBlueprintFunctionNodeSpawner>(Outer);
	NodeSpawner->Field = Function;

	if (NodeClass == nullptr)
	{
		NodeSpawner->NodeClass = UK2Node_CallFunction::StaticClass();
	}
	else
	{
		NodeSpawner->NodeClass = NodeClass;
	}

	//--------------------------------------
	// Default UI Signature
	//--------------------------------------

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	MenuSignature.MenuName = FText::FromString( UK2Node_CallFunction::GetUserFacingFunctionName(Function) );
	MenuSignature.Category = FText::FromString( UK2Node_CallFunction::GetDefaultCategoryForFunction(Function, TEXT("")) );
	MenuSignature.Tooltip  = FText::FromString( UK2Node_CallFunction::GetDefaultTooltipForFunction(Function) );
	// add at least one character, so that PrimeDefaultMenuSignature() doesn't attempt to query the template node
	MenuSignature.Keywords = UK2Node_CallFunction::GetKeywordsForFunction(Function).AppendChar(TEXT(' '));
	MenuSignature.IconName = UK2Node_CallFunction::GetPaletteIconForFunction(Function, MenuSignature.IconTint);

	if (MenuSignature.Category.IsEmpty())
	{
		MenuSignature.Category = BlueprintFunctionNodeSpawnerImpl::FallbackCategory;
	}

	if (MenuSignature.Tooltip.IsEmpty())
	{
		MenuSignature.Tooltip = MenuSignature.MenuName;
	}

	//--------------------------------------
	// Post-Spawn Setup
	//--------------------------------------

	auto SetNodeFunctionLambda = [](UEdGraphNode* NewNode, UField const* Field)
	{
		// user could have changed the node class (to something like
		// UK2Node_BaseAsyncTask, which also wraps a function)
		if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(NewNode))
		{
			FuncNode->SetFromFunction(Cast<UFunction>(Field));
		}
	};
	NodeSpawner->SetNodeFieldDelegate = FSetNodeFieldDelegate::CreateStatic(SetNodeFunctionLambda);


	return NodeSpawner;
}

//------------------------------------------------------------------------------
UBlueprintFunctionNodeSpawner::UBlueprintFunctionNodeSpawner(class FPostConstructInitializeProperties const& PCIP)
	: Super(PCIP)
{
}

//------------------------------------------------------------------------------
void UBlueprintFunctionNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

//------------------------------------------------------------------------------
FBlueprintActionUiSpec UBlueprintFunctionNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	FString const CategoryString = MenuSignature.Category.ToString();
	// FText compares are slow, so let's use FString compares
	bool const bIsUncategorized = (CategoryString == BlueprintFunctionNodeSpawnerImpl::FallbackCategory.ToString());
	if (bIsUncategorized)
	{
		checkSlow(Context.Blueprints.Num() > 0);
		UBlueprint* TargetBlueprint = Context.Blueprints[0];

		// @TODO: this is duplicated in a couple places, move it to some shared resource
		UClass const* TargetClass = (TargetBlueprint->GeneratedClass != nullptr) ? TargetBlueprint->GeneratedClass : TargetBlueprint->ParentClass;
		for (UEdGraphPin* Pin : Context.Pins)
		{
			if ((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object) &&
				Pin->PinType.PinSubCategoryObject.IsValid())
			{
				TargetClass = CastChecked<UClass>(Pin->PinType.PinSubCategoryObject.Get());
			}
		}
		
		UFunction const* WrappedFunction = GetFunction();
		checkSlow(WrappedFunction != nullptr);
		UClass* FunctionClass = WrappedFunction->GetOwnerClass()->GetAuthoritativeClass();

		if (!TargetClass->IsChildOf(FunctionClass))
		{
			MenuSignature.Category = FEditorCategoryUtils::BuildCategoryString(FCommonEditorCategory::Class, 
				FText::FromString(FunctionClass->GetDisplayNameText().ToString() + TEXT("|") + CategoryString));
		}
	}

	if (Bindings.Num() == 1)
	{
		UObjectProperty const* ObjectProperty = Cast<UObjectProperty>(Bindings.CreateConstIterator()->Get());
		if (ObjectProperty != nullptr)
		{
			FString BoundFunctionName = FString::Printf(TEXT("%s (%s)"), *MenuSignature.MenuName.ToString(), *ObjectProperty->GetName());
			// @TODO: this should probably be an FText::Format()
			MenuSignature.MenuName = FText::FromString(BoundFunctionName);
		}
	}
	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

//------------------------------------------------------------------------------
UEdGraphNode* UBlueprintFunctionNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
	auto PostSpawnSetupLambda = [](UEdGraphNode* NewNode, bool bIsTemplateNode, UFunction const* Function, FSetNodeFieldDelegate SetFieldDelegate, FCustomizeNodeDelegate UserDelegate)
	{
		SetFieldDelegate.ExecuteIfBound(NewNode, Function);
		UserDelegate.ExecuteIfBound(NewNode, bIsTemplateNode);
	};
	FCustomizeNodeDelegate PostSpawnSetupDelegate = FCustomizeNodeDelegate::CreateStatic(PostSpawnSetupLambda, GetFunction(), SetNodeFieldDelegate, CustomizeNodeDelegate);

	UClass* SpawnClass = NodeClass;
	if ((Bindings.Num() == 1) && (*Bindings.CreateConstIterator())->IsA<UObjectProperty>())
	{
		SpawnClass = UK2Node_CallFunctionOnMember::StaticClass();
	}
	// if this spawner was set up to spawn a bound node, reset this so the 
	// bound nodes get positioned properly
	BlueprintFunctionNodeSpawnerImpl::BindingOffset = FVector2D::ZeroVector;

	return Super::SpawnNode(SpawnClass, ParentGraph, Bindings, Location, PostSpawnSetupDelegate);
}

//------------------------------------------------------------------------------
bool UBlueprintFunctionNodeSpawner::CanBindMultipleObjects() const
{
	UFunction const* Function = GetFunction();
	check(Function != nullptr);
	return UK2Node_CallFunction::CanFunctionSupportMultipleTargets(Function);
}

//------------------------------------------------------------------------------
bool UBlueprintFunctionNodeSpawner::IsBindingCompatible(UObject const* BindingCandidate) const
{
	bool bCanBind = false;

	UFunction const* Function = GetFunction();
	if ((Function != nullptr) && (NodeClass == UK2Node_CallFunction::StaticClass()))
	{
		UClass* BindingClass = BindingCandidate->GetClass();
		if (UObjectProperty const* ObjectProperty = Cast<UObjectProperty>(BindingCandidate))
		{
			BindingClass = ObjectProperty->PropertyClass;
		}

		if (UClass const* FuncOwner = Function->GetOwnerClass())
		{
			bCanBind = BindingClass->IsChildOf(FuncOwner);
		}
	}

	return bCanBind;
}

//------------------------------------------------------------------------------
bool UBlueprintFunctionNodeSpawner::BindToNode(UEdGraphNode* Node, UObject* Binding) const
{
	return BlueprintFunctionNodeSpawnerImpl::BindFunctionNode(CastChecked<UK2Node_CallFunction>(Node), Binding);
}

//------------------------------------------------------------------------------
UFunction const* UBlueprintFunctionNodeSpawner::GetFunction() const
{
	return Cast<UFunction>(GetField());
}

#undef LOCTEXT_NAMESPACE

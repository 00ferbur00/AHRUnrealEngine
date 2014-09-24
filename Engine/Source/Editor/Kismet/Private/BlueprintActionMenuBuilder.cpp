// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorPrivatePCH.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintDragDropMenuItem.h"
#include "BlueprintBoundMenuItem.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintDelegateNodeSpawner.h"
#include "BlueprintVariableNodeSpawner.h"
#include "K2ActionMenuBuilder.h"		// for FBlueprintPaletteListBuilder/FBlueprintGraphActionListBuilder
#include "EdGraphSchema_K2.h"			// for StaticClass(), bUseLegacyActionMenus, etc.
#include "BlueprintEditor.h"			// for GetMyBlueprintWidget(), and GetIsContextSensitive()
#include "SMyBlueprint.h"				// for SelectionAsVar()
#include "BlueprintEditorUtils.h"		// for FindBlueprintForGraphChecked()
#include "BlueprintEditor.h"			// for GetFocusedGraph()

#define LOCTEXT_NAMESPACE "BlueprintActionMenuBuilder"

/*******************************************************************************
 * FBlueprintActionMenuItemFactory
 ******************************************************************************/

class FBlueprintActionMenuItemFactory
{
public:
	/** 
	 * Menu item factory constructor. Sets up the blueprint context, which
	 * is utilized when configuring blueprint menu items' names/tooltips/etc.
	 *
	 * @param  Context	The blueprint context for the menu being built.
	 */
	FBlueprintActionMenuItemFactory(FBlueprintActionContext const& Context);

	/** A root category to perpend every menu item with */
	FText RootCategory;
	/** The menu sort order to set every menu item with */
	int32 MenuGrouping;
	/** Cached context for the blueprint menu being built */
	FBlueprintActionContext const& Context;
	
	/**
	 * Spawns a new FBlueprintActionMenuItem with the node-spawner. Constructs
	 * the menu item's category, name, tooltip, etc.
	 * 
	 * @param  EditorContext	
	 * @param  Action			The node-spawner that the new menu item should wrap.
	 * @return A newly allocated FBlueprintActionMenuItem (which wraps the supplied action).
	 */
	TSharedPtr<FEdGraphSchemaAction> MakeActionMenuItem(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo const& ActionInfo);

	/**
	 * Spawns a new FBlueprintDragDropMenuItem with the node-spawner. Constructs
	 * the menu item's category, name, tooltip, etc.
	 * 
	 * @param  SampleAction	One of the (possibly) many node-spawners that this menu item is set to represent.
	 * @return A newly allocated FBlueprintActionMenuItem (which wraps the supplied action).
	 */
	TSharedPtr<FBlueprintDragDropMenuItem> MakeDragDropMenuItem(UBlueprintNodeSpawner const* SampleAction);

	/**
	 * 
	 * 
	 * @param  BoundAction	
	 * @return 
	 */
	TSharedPtr<FBlueprintBoundMenuItem> MakeBoundMenuItem(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo const& ActionInfo);
	
private:
	/**
	 * Attempts to pull a menu name from the supplied spawner. If one isn't 
	 * provided, then it spawns a temporary node and pulls one from that node's 
	 * title.
	 * 
	 * @param  EditorContext
	 * @param  Action		The action you want to suss name information from.
	 * @return A name for the menu item wrapping this action.
	 */
	FText GetMenuNameForAction(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo const& ActionInfo);

	/**
	 * Attempts to pull a menu category from the supplied spawner. If one isn't 
	 * provided, then it spawns a temporary node and pulls one from that node.
	 * 
	 * @param  EditorContext
	 * @param  Action		The action you want to suss category information from.
	 * @return A category for the menu item wrapping this action.
	 */
	FText GetCategoryForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action);

	/**
	 * Attempts to pull a menu tooltip from the supplied spawner. If one isn't 
	 * provided, then it spawns a temporary node and pulls one from that node.
	 * 
	 * @param  EditorContext
	 * @param  Action		The action you want to suss tooltip information from.
	 * @return A tooltip for the menu item wrapping this action.
	 */
	FText GetTooltipForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action);

	/**
	 * Attempts to pull a keywords from the supplied spawner. If one isn't 
	 * provided, then it spawns a temporary node and pulls them from that.
	 * 
	 * @TODO: Should search keywords be localized? Probably. 
	 *
	 * @param  EditorContext
	 * @param  Action		The action you want to suss keyword information from.
	 * @return A keyword text string for the menu item wrapping this action.
	 */
	FString GetSearchKeywordsForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action);

	/**
	 * Attempts to pull a menu icon information from the supplied spawner. If
	 * info isn't provided, then it spawns a temporary node and pulls data from 
	 * that node.
	 * 
	 * @param  EditorContext
	 * @param  Action		The action you want to suss icon information from.
	 * @param  ColorOut		The color to tint the icon with.
	 * @return Name of the brush to use (use FEditorStyle::GetBrush() to resolve).
	 */
	FName GetMenuIconForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action, FLinearColor& ColorOut);

	/**
	 * Utility getter function that retrieves the blueprint context for the menu
	 * items being made.
	 * 
	 * @return The first blueprint out of the cached FBlueprintActionContext.
	 */
	UBlueprint* GetTargetBlueprint() const;

	/**
	 * 
	 * 
	 * @param  Action	
	 * @param  EditorContext	
	 * @return 
	 */
	UEdGraphNode* GetTemplateNode(UBlueprintNodeSpawner const* Action, TWeakPtr<FBlueprintEditor> EditorContext) const;	
};

//------------------------------------------------------------------------------
FBlueprintActionMenuItemFactory::FBlueprintActionMenuItemFactory(FBlueprintActionContext const& ContextIn)
	: RootCategory(FText::GetEmpty())
	, MenuGrouping(0)
	, Context(ContextIn)
{
}

//------------------------------------------------------------------------------
TSharedPtr<FEdGraphSchemaAction> FBlueprintActionMenuItemFactory::MakeActionMenuItem(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo const& ActionInfo)
{
	UBlueprintNodeSpawner const* Action = ActionInfo.NodeSpawner;

	FLinearColor IconTint = FLinearColor::White;
	FName IconBrushName = GetMenuIconForAction(EditorContext, Action, IconTint);

	FBlueprintActionMenuItem * NewMenuItem = new FBlueprintActionMenuItem(Action, FEditorStyle::GetBrush(IconBrushName), IconTint, MenuGrouping);
	NewMenuItem->MenuDescription    = GetMenuNameForAction(EditorContext, ActionInfo);
	NewMenuItem->TooltipDescription = GetTooltipForAction(EditorContext, Action).ToString();
	NewMenuItem->Category           = GetCategoryForAction(EditorContext, Action).ToString();
	NewMenuItem->Keywords           = GetSearchKeywordsForAction(EditorContext, Action);

	NewMenuItem->Category = FString::Printf(TEXT("%s|%s"), *RootCategory.ToString(), *NewMenuItem->Category);	
	return MakeShareable(NewMenuItem);
}

//------------------------------------------------------------------------------
TSharedPtr<FBlueprintDragDropMenuItem> FBlueprintActionMenuItemFactory::MakeDragDropMenuItem(UBlueprintNodeSpawner const* SampleAction)
{
	// FBlueprintDragDropMenuItem takes care of its own menu MenuDescription, etc.
	FBlueprintDragDropMenuItem* NewMenuItem = new FBlueprintDragDropMenuItem(Context, SampleAction, MenuGrouping);

	NewMenuItem->Category = FString::Printf(TEXT("%s|%s"), *RootCategory.ToString(), *NewMenuItem->Category);
	return MakeShareable(NewMenuItem);
}

//------------------------------------------------------------------------------
TSharedPtr<FBlueprintBoundMenuItem> FBlueprintActionMenuItemFactory::MakeBoundMenuItem(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo const& ActionInfo)
{
	IBlueprintNodeBinder::FBindingSet const& Bindings = ActionInfo.GetBindings();
	UBlueprintNodeSpawner const* Action = ActionInfo.NodeSpawner;

	FBlueprintBoundMenuItem* NewMenuItem = new FBlueprintBoundMenuItem(Action, MenuGrouping);

	// AddBindings() updates the MenuDescription, everytime a binding is added, 
	// so set the default menu name before it
	NewMenuItem->MenuDescription = GetMenuNameForAction(EditorContext, ActionInfo);
	NewMenuItem->AddBindings(Bindings);

	NewMenuItem->TooltipDescription = GetTooltipForAction(EditorContext, Action).ToString();
	NewMenuItem->Category           = GetCategoryForAction(EditorContext, Action).ToString();
	NewMenuItem->Keywords           = GetSearchKeywordsForAction(EditorContext, Action);

	NewMenuItem->Category = FString::Printf(TEXT("%s|%s"), *RootCategory.ToString(), *NewMenuItem->Category);
	return MakeShareable(NewMenuItem);
}

//------------------------------------------------------------------------------
FText FBlueprintActionMenuItemFactory::GetMenuNameForAction(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo const& ActionInfo)
{
	UBlueprintNodeSpawner const* Action = ActionInfo.NodeSpawner;
	check(Action != nullptr);
	// give the action the chance to save on performance (to keep from having to spawn a template node)
	FText MenuName = Action->GetDefaultMenuName(ActionInfo.GetBindings());
	
	if (MenuName.IsEmpty())
	{
		if (UEdGraphNode* NodeTemplate = GetTemplateNode(Action, EditorContext))
		{
			MenuName = NodeTemplate->GetNodeTitle(ENodeTitleType::MenuTitle);
		}
		else
		{
			// need to give it some name, this is as good as any I guess
			MenuName = FText::FromName(Action->GetFName());
		}
	}
	
	return MenuName;
}

//------------------------------------------------------------------------------
FText FBlueprintActionMenuItemFactory::GetCategoryForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action)
{
	check(Action != nullptr);
	// give the action the chance to save on performance (to keep from having to spawn a template node)
	FText MenuCategory = Action->GetDefaultMenuCategory();
	
	if (MenuCategory.IsEmpty())
	{
		// put uncategorized function calls in a member function category
		// (sorted by their respective classes)
		if (UBlueprintFunctionNodeSpawner const* FuncSpawner = Cast<UBlueprintFunctionNodeSpawner>(Action))
		{
			UFunction const* Function = FuncSpawner->GetFunction();
			check(Function != nullptr);
			UClass* FuncOwner = Function->GetOuterUClass();

			UBlueprint* Blueprint = GetTargetBlueprint();
			check(Blueprint != nullptr);
			UClass* BlueprintClass = (Blueprint->SkeletonGeneratedClass != nullptr) ? Blueprint->SkeletonGeneratedClass : Blueprint->ParentClass;

			// if this is NOT a self function call (self function calls
			// don't get nested any deeper)
			if (!BlueprintClass->IsChildOf(FuncOwner))
			{
				MenuCategory = FuncOwner->GetDisplayNameText();
			}
			MenuCategory = FText::Format(LOCTEXT("MemberFunctionsCategory", "{0}|Call Function"), MenuCategory);
		}
		else if (UBlueprintNodeSpawner const* NodeSpawner = Cast<UBlueprintNodeSpawner>(Action))
		{
			// Only for macro instances do we want to mess with the category to put it into a category
			// named after the Blueprint if no other category is specified
			if(NodeSpawner->NodeClass == UK2Node_MacroInstance::StaticClass())
			{
				UK2Node_MacroInstance* MacroInstance = Cast<UK2Node_MacroInstance>(NodeSpawner->GetTemplateNode());
				if(UEdGraph* MacroGraph = MacroInstance->GetMacroGraph())
				{
					// Check if the MacroGraph has a category
					FKismetUserDeclaredFunctionMetadata* MacroGraphMetadata = UK2Node_MacroInstance::GetAssociatedGraphMetadata(MacroGraph);
					if ((MacroGraphMetadata != nullptr) && MacroGraphMetadata->Category.IsEmpty())
					{
						MenuCategory = MacroInstance->GetMenuCategory();

						UBlueprint* MacroBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(MacroGraph);
						if(MacroBlueprint != GetTargetBlueprint())
						{
							FText BlueprintDisplayName = MacroBlueprint->GeneratedClass->GetDisplayNameText();
							FFormatNamedArguments Args;
							Args.Add(TEXT("BlueprintDisplayName"), BlueprintDisplayName);
							Args.Add(TEXT("MacroCategory"), MenuCategory);

							MenuCategory = FText::Format(LOCTEXT("MemberMacroCategory", "{BlueprintDisplayName}|{MacroCategory}"), Args);
						}
					}
				}
			}
		}

		// If the menu category is still empty, fill it in with the node template's defined category
		if(MenuCategory.IsEmpty())
		{
			// @TODO: consider moving GetMenuCategory() up into UEdGraphNode
			if (UK2Node* NodeTemplate = Cast<UK2Node>(GetTemplateNode(Action, EditorContext)))
			{
				MenuCategory = NodeTemplate->GetMenuCategory();
			}
		}
	}
	else
	{
		if (UBlueprintVariableNodeSpawner const* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Action))
		{
			UProperty const* Property = VarSpawner->GetVarProperty();
			if(Property)
			{
				check(Property != nullptr);
				UClass* PropertyOwner = Property->GetTypedOuter<UClass>();

				UBlueprint* Blueprint = GetTargetBlueprint();
				check(Blueprint != nullptr);
				UClass* BlueprintClass = (Blueprint->SkeletonGeneratedClass != nullptr) ? Blueprint->SkeletonGeneratedClass : Blueprint->ParentClass;

				// if this is NOT a self function call (self function calls
				// don't get nested any deeper)
				if (!BlueprintClass->IsChildOf(PropertyOwner))
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("PropertyDisplayName"), PropertyOwner->GetDisplayNameText());
					Args.Add(TEXT("VariableCategory"), MenuCategory);
					MenuCategory = FText::Format(LOCTEXT("MemberVariablesCategory", "{PropertyDisplayName}|{VariableCategory}"), Args);
				}
			}
		}
	}
	
	return MenuCategory;
}

//------------------------------------------------------------------------------
FText FBlueprintActionMenuItemFactory::GetTooltipForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action)
{
	check(Action != nullptr);
	// give the action the chance to save on performance (to keep from having to spawn a template node)
	FText Tooltip = Action->GetDefaultMenuTooltip();
	
	if (Tooltip.IsEmpty())
	{
		if (UEdGraphNode* NodeTemplate = GetTemplateNode(Action, EditorContext))
		{
			Tooltip = NodeTemplate->GetTooltipText();
		}
	}
	
	return Tooltip;
}

//------------------------------------------------------------------------------
FString FBlueprintActionMenuItemFactory::GetSearchKeywordsForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action)
{
	check(Action != nullptr);
	// give the action the chance to save on performance (to keep from having to 
	// spawn a template node)
	//
	// @TODO: Should search keywords be localized? Probably.
	FString SearchKeywords = Action->GetDefaultSearchKeywords();
	
	if (SearchKeywords.IsEmpty())
	{
		if (UEdGraphNode* NodeTemplate = GetTemplateNode(Action, EditorContext))
		{
			SearchKeywords = NodeTemplate->GetKeywords();
		}
	}
	
	return SearchKeywords;
}

//------------------------------------------------------------------------------
FName FBlueprintActionMenuItemFactory::GetMenuIconForAction(TWeakPtr<FBlueprintEditor> EditorContext, UBlueprintNodeSpawner const* Action, FLinearColor& ColorOut)
{
	FName BrushName = Action->GetDefaultMenuIcon(ColorOut);
	if (BrushName.IsNone())
	{
		if (UEdGraphNode* NodeTemplate = GetTemplateNode(Action, EditorContext))
		{
			BrushName = NodeTemplate->GetPaletteIcon(ColorOut);
		}
	}
	return BrushName;
}

//------------------------------------------------------------------------------
UBlueprint* FBlueprintActionMenuItemFactory::GetTargetBlueprint() const
{
	UBlueprint* TargetBlueprint = nullptr;
	if (Context.Blueprints.Num() > 0)
	{
		TargetBlueprint = Context.Blueprints[0];
	}
	return TargetBlueprint;
}

//------------------------------------------------------------------------------
UEdGraphNode* FBlueprintActionMenuItemFactory::GetTemplateNode(UBlueprintNodeSpawner const* Action, TWeakPtr<FBlueprintEditor> EditorContext) const
{
	UEdGraph* TargetGraph = nullptr;
	if (Context.Graphs.Num() > 0)
	{
		TargetGraph = Context.Graphs[0];
	}
	else
	{
		UBlueprint* Blueprint = GetTargetBlueprint();
		check(Blueprint != nullptr);
		
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			TargetGraph = Blueprint->UbergraphPages[0];
		}
		else if (EditorContext.IsValid())
		{
			TargetGraph = EditorContext.Pin()->GetFocusedGraph();
		}
	}

	check(Action != nullptr);
	return Action->GetTemplateNode(TargetGraph);
}

/*******************************************************************************
 * Static FBlueprintActionMenuBuilder Helpers
 ******************************************************************************/

namespace FBlueprintActionMenuBuilderImpl
{
	typedef TArray< TSharedPtr<FEdGraphSchemaAction> > MenuItemList;

	/** Defines a sub-section of the overall blueprint menu (filter, heading, etc.) */
	struct FMenuSectionDefinition
	{
	public:
		FMenuSectionDefinition(FBlueprintActionFilter const& SectionFilter, uint32 const Flags);

		/** Series of ESectionFlags, aimed at customizing how we construct this menu section */
		uint32 Flags;
		/** A filter for this section of the menu */
		FBlueprintActionFilter Filter;
		
		/** Sets the root category for menu items in this section. */
		void SetSectionHeading(FText const& RootCategory);
		/** Gets the root category for menu items in this section. */
		FText const& GetSectionHeading() const;

		/** Sets the grouping for menu items belonging to this section. */
		void SetSectionSortOrder(int32 const MenuGrouping);
		
		/**
		 * Filters the supplied action and if it passes, spawns a new 
		 * FBlueprintActionMenuItem for the specified menu (does not add the 
		 * item to the menu-builder itself).
		 *
		 * @param  EditorContext	
		 * @param  DatabaseAction	The node-spawner that the new menu item should wrap.
		 * @return An empty TSharedPtr if the action was filtered out, otherwise a newly allocated FBlueprintActionMenuItem.
		 */
		MenuItemList MakeMenuItems(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo& DatabaseAction);

		/**
		 * 
		 * 
		 * @param  EditorContext	
		 * @param  DatabaseAction	
		 * @param  Bindings	
		 * @return 
		 */
		void AddBoundMenuItems(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo& DatabaseAction, TArray<UObject*> const& Bindings, MenuItemList& MenuItemsOut);
		
		/**
		 * Clears out any consolidated properties that this may have been 
		 * tracking (so we can start a new and spawn new consolidated menu items).
		 */
		void Empty();
		
	private:
		/** In charge of spawning menu items for this section (holds category/ordering information)*/
		FBlueprintActionMenuItemFactory ItemFactory;
		/** Tracks the properties that we've already consolidated and passed (when using the ConsolidatePropertyActions flag)*/
		TMap<UProperty const*, TSharedPtr<FBlueprintDragDropMenuItem>> ConsolidatedProperties;
	};
	
	/**
	 * To offer a fallback in case this menu system is unstable on release, this
	 * method implements the old way we used collect blueprint menu actions (for
	 * both the palette and context menu).
	 *
	 * @param  MenuSection		The primary section for the FBlueprintActionMenuBuilder.
	 * @param  BlueprintEditor	
	 * @param  MenuOut  		The menu builder we want all the legacy actions appended to.
	 */
	static void AppendLegacyItems(FMenuSectionDefinition const& MenuDef, TWeakPtr<FBlueprintEditor> BlueprintEditor, FBlueprintActionMenuBuilder& MenuOut);

	/**
	 * 
	 * 
	 * @param  Context	
	 * @return 
	 */
	static TArray<UObject*> GetBindingCandidates(FBlueprintActionContext const& Context);
}

//------------------------------------------------------------------------------
FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::FMenuSectionDefinition(FBlueprintActionFilter const& SectionFilterIn, uint32 const FlagsIn)
	: Flags(FlagsIn)
	, Filter(SectionFilterIn)
	, ItemFactory(Filter.Context)
{
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::SetSectionHeading(FText const& RootCategory)
{
	ItemFactory.RootCategory = RootCategory;
}

//------------------------------------------------------------------------------
FText const& FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::GetSectionHeading() const
{
	return ItemFactory.RootCategory;
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::SetSectionSortOrder(int32 const MenuGrouping)
{
	ItemFactory.MenuGrouping = MenuGrouping;
}
// 
//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::AddBoundMenuItems(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo& DatabaseActionInfo, TArray<UObject*> const& PerspectiveBindings, MenuItemList& MenuItemsOut)
{
	UBlueprintNodeSpawner const* DatabaseAction = DatabaseActionInfo.NodeSpawner;

	TSharedPtr<FBlueprintBoundMenuItem> LastMadeMenuItem;
	bool const bConsolidate = (Flags & FBlueprintActionMenuBuilder::ConsolidateBoundActions) != 0;
	
	IBlueprintNodeBinder::FBindingSet CompatibleBindings;
	// we don't want the blueprint database growing out of control with an entry 
	// for every object you could ever possibly bind to, so each 
	// UBlueprintNodeSpawner comes with an interface to test/bind through... 
	for (auto BindingIt = PerspectiveBindings.CreateConstIterator(); BindingIt;)
	{
		UObject const* BindingObj = *BindingIt;
		++BindingIt;
		bool const bIsLastBinding = !BindingIt;

		// check to see if this object can be bound to this action
		if (DatabaseAction->IsBindingCompatible(BindingObj))
		{
			// add bindings before filtering (in case tests accept/reject based off of this)
			CompatibleBindings.Add(BindingObj);
		}
// 		else if (bConsolidate)
// 		{
// 			MadeMenuItems.Empty();
// 			LastMadeMenuItem.Reset();
// 			break;
// 		}

		// if BoundAction is now "full" (meaning it can take any more 
		// bindings), or if this is the last binding to test...
		if ((CompatibleBindings.Num() > 0) && (!DatabaseAction->CanBindMultipleObjects() || bIsLastBinding || !bConsolidate))
		{
			// we don't want binding to mutate DatabaseActionInfo, so we clone  
			// the action info, and tack on some binding data
			FBlueprintActionInfo BoundActionInfo(DatabaseActionInfo, CompatibleBindings);

			// have to check IsFiltered() for every "fully bound" action (in
			// case there are tests that reject based off of this), we may 
			// test this multiple times per action (we have to make sure 
			// that every set of bound objects pass before folding them into
			// MenuItem)
			bool const bPassedFilter = !Filter.IsFiltered(BoundActionInfo);
			if (bPassedFilter)
			{
				if (!bConsolidate || !LastMadeMenuItem.IsValid())
				{
					LastMadeMenuItem = ItemFactory.MakeBoundMenuItem(EditorContext, BoundActionInfo);
					MenuItemsOut.Add(LastMadeMenuItem);
				}
				else
				{
					// move these bindings over to the menu item (so we can 
					// test the next set)
					LastMadeMenuItem->AddBindings(CompatibleBindings);
				}
			}
			CompatibleBindings.Empty(); // do before we copy back over cached fields for DatabaseActionInfo

			// copy over any fields that got cached for filtering (with
			// an empty binding set)
			/*DatabaseActionInfo = FBlueprintActionInfo(BoundActionInfo, CompatibleBindings);*/
		}
	}
}

//------------------------------------------------------------------------------
FBlueprintActionMenuBuilderImpl::MenuItemList FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::MakeMenuItems(TWeakPtr<FBlueprintEditor> EditorContext, FBlueprintActionInfo& DatabaseAction)
{	
	TSharedPtr<FEdGraphSchemaAction> UnBoundMenuEntry;
	bool bPassedFilter = !Filter.IsFiltered(DatabaseAction);

	// if the caller wants to consolidate all property actions, then we have to 
	// check and see if this is one of those that needs consolidating (needs 
	// a FBlueprintDragDropMenuItem instead of a FBlueprintActionMenuItem)
	if (bPassedFilter && (Flags & FBlueprintActionMenuBuilder::ConsolidatePropertyActions))
	{
		UProperty const* ActionProperty = nullptr;
		if (UBlueprintVariableNodeSpawner const* VariableSpawner = Cast<UBlueprintVariableNodeSpawner>(DatabaseAction.NodeSpawner))
		{
			ActionProperty = VariableSpawner->GetVarProperty();
			bPassedFilter = (ActionProperty != nullptr);
		}
		else if (UBlueprintDelegateNodeSpawner const* DelegateSpawner = Cast<UBlueprintDelegateNodeSpawner>(DatabaseAction.NodeSpawner))
		{
			ActionProperty = DelegateSpawner->GetProperty();
			bPassedFilter = (ActionProperty != nullptr);
		}

		if (ActionProperty != nullptr)
		{
			if (TSharedPtr<FBlueprintDragDropMenuItem>* ConsolidatedMenuItem = ConsolidatedProperties.Find(ActionProperty))
			{
				(*ConsolidatedMenuItem)->AppendAction(DatabaseAction.NodeSpawner);
				// this menu entry has already been returned, don't need to 
				// create/insert a new one
				bPassedFilter = false;
			}
			else
			{
				TSharedPtr<FBlueprintDragDropMenuItem> NewMenuItem = ItemFactory.MakeDragDropMenuItem(DatabaseAction.NodeSpawner);
				ConsolidatedProperties.Add(ActionProperty, NewMenuItem);
				UnBoundMenuEntry = NewMenuItem;
			}
		}
	}

	if (!UnBoundMenuEntry.IsValid() && bPassedFilter)
	{
		UnBoundMenuEntry = ItemFactory.MakeActionMenuItem(EditorContext, DatabaseAction);
	}

	FBlueprintActionMenuBuilderImpl::MenuItemList MenuItems;
	if (UnBoundMenuEntry.IsValid())
	{
		MenuItems.Add(UnBoundMenuEntry);
	}
	AddBoundMenuItems(EditorContext, DatabaseAction, GetBindingCandidates(Filter.Context), MenuItems);

	return MenuItems;
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilderImpl::FMenuSectionDefinition::Empty()
{
	ConsolidatedProperties.Empty();
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilderImpl::AppendLegacyItems(FMenuSectionDefinition const& MenuDef, TWeakPtr<FBlueprintEditor> BlueprintEditor, FBlueprintActionMenuBuilder& MenuOut)
{
	FBlueprintActionFilter const&  MenuFilter  = MenuDef.Filter;
	FBlueprintActionContext const& MenuContext = MenuFilter.Context;
	
	// if this is for the context menu
	if (MenuContext.Graphs.Num() > 0)
	{
		UEdGraph* Graph = MenuContext.Graphs[0];
		UEdGraphSchema const* GraphSchema = GetDefault<UEdGraphSchema>(Graph->Schema);
		
		FBlueprintGraphActionListBuilder LegacyBuilder(Graph);
		if (MenuContext.Pins.Num() > 0)
		{
			LegacyBuilder.FromPin = MenuContext.Pins[0];
		}
		
		bool bIsContextSensitive = true;
		if (BlueprintEditor.IsValid())
		{
			bIsContextSensitive = BlueprintEditor.Pin()->GetIsContextSensitive();
			if (bIsContextSensitive)
			{
				FEdGraphSchemaAction_K2Var* SelectedVar = BlueprintEditor.Pin()->GetMyBlueprintWidget()->SelectionAsVar();
				if ((SelectedVar != nullptr) && (SelectedVar->GetProperty() != nullptr))
				{
					LegacyBuilder.SelectedObjects.Add(SelectedVar->GetProperty());
				}
			}
		}
		
		if (bIsContextSensitive)
		{
			GraphSchema->GetGraphContextActions(LegacyBuilder);
			MenuOut.Append(LegacyBuilder);
		}
		else
		{
			UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked(Graph);
			FBlueprintPaletteListBuilder ContextlessLegacyBuilder(Blueprint);
			UEdGraphSchema_K2::GetAllActions(ContextlessLegacyBuilder);
			MenuOut.Append(ContextlessLegacyBuilder);
		}
	}
	else if (MenuContext.Blueprints.Num() > 0)
	{
		UBlueprint* Blueprint = MenuContext.Blueprints[0];
		FBlueprintPaletteListBuilder LegacyBuilder(Blueprint, MenuDef.GetSectionHeading().ToString());
		
		UClass* ClassFilter = nullptr;
		if (MenuFilter.TargetClasses.Num() > 0)
		{
			ClassFilter = MenuFilter.TargetClasses[0];
		}
		
		UEdGraphSchema_K2 const* K2Schema = GetDefault<UEdGraphSchema_K2>();
		FK2ActionMenuBuilder(K2Schema).GetPaletteActions(LegacyBuilder, ClassFilter);
		
		MenuOut.Append(LegacyBuilder);
	}
}

//------------------------------------------------------------------------------
static TArray<UObject*> FBlueprintActionMenuBuilderImpl::GetBindingCandidates(FBlueprintActionContext const& Context)
{
	return Context.SelectedObjects;
}

/*******************************************************************************
 * FBlueprintActionMenuBuilder
 ******************************************************************************/

//------------------------------------------------------------------------------
FBlueprintActionMenuBuilder::FBlueprintActionMenuBuilder(TWeakPtr<FBlueprintEditor> InBlueprintEditorPtr)
	: BlueprintEditorPtr(InBlueprintEditorPtr)
{
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilder::Empty()
{
	FGraphActionListBuilderBase::Empty();
	MenuSections.Empty();
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilder::AddMenuSection(FBlueprintActionFilter const& Filter, FText const& Heading/* = FText::GetEmpty()*/, int32 MenuOrder/* = 0*/, uint32 const Flags/* = 0*/)
{
	using namespace FBlueprintActionMenuBuilderImpl;
	
	TSharedRef<FMenuSectionDefinition> SectionDescRef = MakeShareable(new FMenuSectionDefinition(Filter, Flags));
	SectionDescRef->SetSectionHeading(Heading);
	SectionDescRef->SetSectionSortOrder(MenuOrder);

	MenuSections.Add(SectionDescRef);
}

//------------------------------------------------------------------------------
void FBlueprintActionMenuBuilder::RebuildActionList()
{
	using namespace FBlueprintActionMenuBuilderImpl;

	FGraphActionListBuilderBase::Empty();
	for (TSharedRef<FMenuSectionDefinition> MenuSection : MenuSections)
	{
		// clear out intermediate actions that may have been spawned (like 
		// consolidated property actions).
		MenuSection->Empty();
	}
	
	UEditorExperimentalSettings const* ExperimentalSettings = GetDefault<UEditorExperimentalSettings>();
	if (ExperimentalSettings->bUseRefactoredBlueprintMenuingSystem)
	{
		FBlueprintActionDatabase::FActionRegistry const& ActionDatabase = FBlueprintActionDatabase::Get().GetAllActions();
		for (auto const& ActionEntry : ActionDatabase)
		{
			for (UBlueprintNodeSpawner const* NodeSpawner : ActionEntry.Value)
			{

				FBlueprintActionInfo BlueprintAction(ActionEntry.Key, NodeSpawner);

				// @TODO: could probably have a super filter that spreads across 
				//        all MenuSctions (to pair down on performance?)
				for (TSharedRef<FMenuSectionDefinition> const& MenuSection : MenuSections)
				{
					for (TSharedPtr<FEdGraphSchemaAction> MenuEntry : MenuSection->MakeMenuItems(BlueprintEditorPtr, BlueprintAction))
					{
						AddAction(MenuEntry);
					}
				}
			}
		}	
	}
	else if (MenuSections.Num() > 0)
	{
		AppendLegacyItems(*MenuSections[0], BlueprintEditorPtr, *this);
	}

	// @TODO: account for all K2ActionMenuBuilder action types...
	// - FEdGraphSchemaAction_K2AddTimeline
	// - FEdGraphSchemaAction_K2ViewNode
	// - FEdGraphSchemaAction_K2AddCustomEvent
	//   FEdGraphSchemaAction_EventFromFunction
	// - FEdGraphSchemaAction_K2Var
	// - FEdGraphSchemaAction_K2Delegate
	// - FEdGraphSchemaAction_K2AssignDelegate
	// - FEdGraphSchemaAction_K2AddComment
	// - FEdGraphSchemaAction_K2PasteHere
	// - FEdGraphSchemaAction_K2NewNode
	// - FEdGraphSchemaAction_Dummy
	//   FEdGraphSchemaAction_K2AddCallOnActor
	//   FEdGraphSchemaAction_K2AddCallOnVariable
	// - FEdGraphSchemaAction_K2AddComponent
	// - FEdGraphSchemaAction_K2AddComment
}

#undef LOCTEXT_NAMESPACE

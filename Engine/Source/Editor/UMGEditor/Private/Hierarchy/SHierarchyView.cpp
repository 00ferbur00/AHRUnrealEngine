// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"
#include "SHierarchyView.h"
#include "SHierarchyViewItem.h"

#include "UMGEditorActions.h"

#include "PreviewScene.h"
#include "SceneViewport.h"

#include "BlueprintEditor.h"
#include "SKismetInspector.h"
#include "BlueprintEditorUtils.h"
#include "WidgetTemplateClass.h"
#include "WidgetBlueprintEditor.h"
#include "SSearchBox.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

#include "Components/PanelWidget.h"

#define LOCTEXT_NAMESPACE "UMG"

void SHierarchyView::Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor, USimpleConstructionScript* InSCS)
{
	BlueprintEditor = InBlueprintEditor;
	bRebuildTreeRequested = false;

	// register for any objects replaced
	GEditor->OnObjectsReplaced().AddRaw(this, &SHierarchyView::OnObjectsReplaced);

	// Create the filter for searching in the tree
	SearchBoxWidgetFilter = MakeShareable(new WidgetTextFilter(WidgetTextFilter::FItemToStringArray::CreateSP(this, &SHierarchyView::TransformWidgetToString)));

	UWidgetBlueprint* Blueprint = GetBlueprint();
	Blueprint->OnChanged().AddRaw(this, &SHierarchyView::OnBlueprintChanged);

	FilterHandler = MakeShareable(new TreeFilterHandler< TSharedPtr<FHierarchyModel> >());
	FilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	FilterHandler->SetRootItems(&RootWidgets, &TreeRootWidgets);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler< TSharedPtr<FHierarchyModel> >::FOnGetChildren::CreateRaw(this, &SHierarchyView::WidgetHierarchy_OnGetChildren));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchWidgets", "Search Widgets"))
				.OnTextChanged(this, &SHierarchyView::OnSearchChanged)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(TreeViewArea, SBorder)
				.Padding(0)
				.BorderImage( FEditorStyle::GetBrush( "NoBrush" ) )
			]
		]
	];

	RebuildTreeView();

	BlueprintEditor.Pin()->OnSelectedWidgetsChanged.AddRaw(this, &SHierarchyView::OnEditorSelectionChanged);

	bRefreshRequested = true;
}

SHierarchyView::~SHierarchyView()
{
	UWidgetBlueprint* Blueprint = GetBlueprint();
	if ( Blueprint )
	{
		Blueprint->OnChanged().RemoveAll(this);
	}

	if ( BlueprintEditor.IsValid() )
	{
		BlueprintEditor.Pin()->OnSelectedWidgetsChanged.RemoveAll(this);
	}

	GEditor->OnObjectsReplaced().RemoveAll(this);
}

void SHierarchyView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if ( bRebuildTreeRequested || bRefreshRequested )
	{
		if ( bRebuildTreeRequested )
		{
			RebuildTreeView();
		}

		SaveExpandedItems();

		RefreshTree();

		RestoreExpandedItems();

		OnEditorSelectionChanged();

		bRefreshRequested = false;
		bRebuildTreeRequested = false;
	}
}

FReply SHierarchyView::OnKeyDown(const FGeometry& MyGeometry, const FKeyboardEvent& InKeyboardEvent)
{
	BlueprintEditor.Pin()->PasteDropLocation = FVector2D(0, 0);

	if ( BlueprintEditor.Pin()->DesignerCommandList->ProcessCommandBindings(InKeyboardEvent) )
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SHierarchyView::TransformWidgetToString(TSharedPtr<FHierarchyModel> Item, OUT TArray< FString >& Array)
{
	Array.Add( Item->GetText().ToString() );
}

void SHierarchyView::OnSearchChanged(const FText& InFilterText)
{
	bRefreshRequested = true;
	FilterHandler->SetIsEnabled(!InFilterText.IsEmpty());
	SearchBoxWidgetFilter->SetRawFilterText(InFilterText);
}

FText SHierarchyView::GetSearchText() const
{
	return SearchBoxWidgetFilter->GetRawFilterText();
}

void SHierarchyView::OnEditorSelectionChanged()
{
	WidgetTreeView->ClearSelection();

	if ( RootWidgets.Num() > 0 )
	{
		RootWidgets[0]->RefreshSelection();
	}

	RestoreSelectedItems();
}

UWidgetBlueprint* SHierarchyView::GetBlueprint() const
{
	if ( BlueprintEditor.IsValid() )
	{
		UBlueprint* BP = BlueprintEditor.Pin()->GetBlueprintObj();
		return CastChecked<UWidgetBlueprint>(BP);
	}

	return NULL;
}

void SHierarchyView::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	if ( InBlueprint )
	{
		bRefreshRequested = true;
	}
}

TSharedPtr<SWidget> SHierarchyView::WidgetHierarchy_OnContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, NULL);

	FWidgetBlueprintEditorUtils::CreateWidgetContextMenu(MenuBuilder, BlueprintEditor.Pin().ToSharedRef(), FVector2D(0, 0));

	return MenuBuilder.MakeWidget();
}

void SHierarchyView::WidgetHierarchy_OnGetChildren(TSharedPtr<FHierarchyModel> InParent, TArray< TSharedPtr<FHierarchyModel> >& OutChildren)
{
	InParent->GatherChildren(OutChildren);
}

TSharedRef< ITableRow > SHierarchyView::WidgetHierarchy_OnGenerateRow(TSharedPtr<FHierarchyModel> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SHierarchyViewItem, OwnerTable, InItem)
		.HighlightText(this, &SHierarchyView::GetSearchText);
}

void SHierarchyView::WidgetHierarchy_OnSelectionChanged(TSharedPtr<FHierarchyModel> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if ( SelectedItem.IsValid() && SelectInfo != ESelectInfo::Direct )
	{
		SelectedItem->OnSelection();
	}
}

FReply SHierarchyView::HandleDeleteSelected()
{
	TSet<FWidgetReference> SelectedWidgets = BlueprintEditor.Pin()->GetSelectedWidgets();
	
	FWidgetBlueprintEditorUtils::DeleteWidgets(GetBlueprint(), SelectedWidgets);

	return FReply::Handled();
}

void SHierarchyView::RefreshTree()
{
	RootWidgets.Empty();
	RootWidgets.Add( MakeShareable(new FHierarchyRoot(BlueprintEditor.Pin())) );

	FilterHandler->RefreshAndFilterTree();
}

void SHierarchyView::RebuildTreeView()
{
	SAssignNew(WidgetTreeView, STreeView< TSharedPtr<FHierarchyModel> >)
		.ItemHeight(20.0f)
		.SelectionMode(ESelectionMode::Single)
		.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler< TSharedPtr<FHierarchyModel> >::OnGetFilteredChildren)
		.OnGenerateRow(this, &SHierarchyView::WidgetHierarchy_OnGenerateRow)
		.OnSelectionChanged(this, &SHierarchyView::WidgetHierarchy_OnSelectionChanged)
		.OnContextMenuOpening(this, &SHierarchyView::WidgetHierarchy_OnContextMenuOpening)
		.TreeItemsSource(&TreeRootWidgets);

	FilterHandler->SetTreeView(WidgetTreeView.Get());

	TreeViewArea->SetContent(
		SNew(SScrollBorder, WidgetTreeView.ToSharedRef())
		[
			WidgetTreeView.ToSharedRef()
		]);
}

void SHierarchyView::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	if ( !bRebuildTreeRequested )
	{
		bRefreshRequested = true;
		bRebuildTreeRequested = true;

		// We save the expanded items immediately because they're potentially about to become invalid.
		SaveExpandedItems();
	}
}

void SHierarchyView::SaveExpandedItems()
{
	if ( ExpandedItems.Num() == 0 )
	{
		TSet < TSharedPtr<FHierarchyModel> > ExpandedModels;
		WidgetTreeView->GetExpandedItems(ExpandedModels);

		for ( TSharedPtr<FHierarchyModel>& Model : ExpandedModels )
		{
			ExpandedItems.Add(Model->GetUniqueName());
		}
	}
}

void SHierarchyView::RestoreExpandedItems()
{
	for ( TSharedPtr<FHierarchyModel>& Model : RootWidgets )
	{
		RecursiveExpand(Model);
	}

	ExpandedItems.Empty();
}

void SHierarchyView::RecursiveExpand(TSharedPtr<FHierarchyModel>& Model)
{
	if ( ExpandedItems.Contains(Model->GetUniqueName()) )
	{
		WidgetTreeView->SetItemExpansion(Model, true);

		TArray< TSharedPtr<FHierarchyModel> > Children;
		Model->GatherChildren(Children);

		for ( TSharedPtr<FHierarchyModel>& ChildModel : Children )
		{
			RecursiveExpand(ChildModel);
		}
	}
}

void SHierarchyView::RestoreSelectedItems()
{
	for ( TSharedPtr<FHierarchyModel>& Model : RootWidgets )
	{
		RecursiveSelection(Model);
	}
}

void SHierarchyView::RecursiveSelection(TSharedPtr<FHierarchyModel>& Model)
{
	if ( Model->ContainsSelection() )
	{
		// Expand items that contain selection.
		WidgetTreeView->SetItemExpansion(Model, true);

		TArray< TSharedPtr<FHierarchyModel> > Children;
		Model->GatherChildren(Children);

		for ( TSharedPtr<FHierarchyModel>& ChildModel : Children )
		{
			RecursiveSelection(ChildModel);
		}
	}

	if ( Model->IsSelected() )
	{
		WidgetTreeView->SetItemSelection(Model, true, ESelectInfo::Direct);
		WidgetTreeView->RequestScrollIntoView(Model);
	}
}


//@TODO UMG Drop widgets onto the tree, when nothing is present, if there is a root node present, what happens then, let the root node attempt to place it?

#undef LOCTEXT_NAMESPACE

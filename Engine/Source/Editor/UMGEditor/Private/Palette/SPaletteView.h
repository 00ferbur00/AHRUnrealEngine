// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SCompoundWidget.h"
#include "BlueprintEditor.h"

#include "WidgetTemplate.h"

/** View model for the items in the widget template list */
class FWidgetViewModel : public TSharedFromThis<FWidgetViewModel>
{
public:
	virtual FText GetName() const = 0;

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) = 0;

	virtual void GetChildren(TArray< TSharedPtr<FWidgetViewModel> >& OutChildren)
	{
	}
};

/**  */
class SPaletteView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SPaletteView ){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FBlueprintEditor> InBlueprintEditor, USimpleConstructionScript* InSCS);
	virtual ~SPaletteView();

private:
	UWidgetBlueprint* GetBlueprint() const;

	void BuildWidgetList();
	void BuildClassWidgetList();
	void BuildSpecialWidgetList();

	void OnGetChildren(TSharedPtr<FWidgetViewModel> Item, TArray< TSharedPtr<FWidgetViewModel> >& Children);
	TSharedRef<ITableRow> OnGenerateWidgetTemplateItem(TSharedPtr<FWidgetViewModel> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Called when the filter text is changed. */
	void OnSearchChanged(const FText& InFilterText);

private:
	void LoadItemExpanssion();
	void SaveItemExpansion();

	void AddWidgetTemplate(TSharedPtr<FWidgetTemplate> Template);

	TWeakPtr<class FBlueprintEditor> BlueprintEditor;

	typedef TArray< TSharedPtr<FWidgetTemplate> > WidgetTemplateArray;
	TMap< FString, WidgetTemplateArray > WidgetTemplateCategories;

	typedef TArray< TSharedPtr<FWidgetViewModel> > ViewModelsArray;
	ViewModelsArray WidgetViewModels;

	TSharedPtr< STreeView< TSharedPtr<FWidgetViewModel> > > WidgetTemplatesView;

	bool bRefreshRequested;
	FText SearchText;
};

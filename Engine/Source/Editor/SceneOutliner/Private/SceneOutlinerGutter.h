// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutliner.h"
#include "ISceneOutlinerColumn.h"
#include "SceneOutlinerInitializationOptions.h"

DECLARE_DELEGATE_TwoParams(FOnSetItemVisibility, TSharedRef<SceneOutliner::TOutlinerTreeItem>, bool)

/**
 * A gutter for the SceneOutliner which is capable of displaying a variety of Actor details
 */
class FSceneOutlinerGutter : public ISceneOutlinerColumn
{

public:

	/**	Constructor */
	FSceneOutlinerGutter(FOnSetItemVisibility InOnSetItemVisibility);

	virtual ~FSceneOutlinerGutter() {}

	// -----------------------------------------
	// ISceneOutlinerColumn Implementation
	virtual FName GetColumnID() override;

	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;

	virtual const TSharedRef< SWidget > ConstructRowWidget( const TSharedRef<SceneOutliner::TOutlinerTreeItem> TreeItem ) override;

	virtual bool ProvidesSearchStrings() override { return false; }

	virtual void PopulateActorSearchStrings(const AActor* const Actor, OUT TArray< FString >& OutSearchStrings) const override {}

	virtual bool SupportsSorting() const override { return true; }

	virtual void SortItems(TArray<TSharedPtr<SceneOutliner::TOutlinerTreeItem>>& RootItems, const EColumnSortMode::Type SortMode) const override;
	// -----------------------------------------

private:

	/** A delegate to execute when we need to set the visibility of an item */
	FOnSetItemVisibility OnSetItemVisibility;
};
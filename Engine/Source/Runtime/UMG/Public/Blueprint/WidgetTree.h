// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WidgetTree.generated.h"

/** The widget tree manages the collection of widgets in a blueprint widget. */
UCLASS()
class UMG_API UWidgetTree : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	/** Finds the widget in the tree by name. */
	UWidget* FindWidget(const FString& Name) const;

	/** Finds a widget in the tree using the native widget as the key. */
	UWidget* FindWidget(TSharedRef<SWidget> InWidget) const;

	/** Removes the widget from the hierarchy and all sub widgets. */
	bool RemoveWidget(UWidget* Widget);

	/** Gets the parent widget of a given widget, and potentially the child index. */
	class UPanelWidget* FindWidgetParent(UWidget* Widget, int32& OutChildIndex);

	/** Gathers all the widgets in the tree recursively */
	void GetAllWidgets(TArray<UWidget*>& Widgets) const;

	/** Gathers only the immediate child widgets of a parent widget. */
	void GetChildWidgets(UWidget* Parent, TArray<UWidget*>& Widgets) const;

	/** Constructs the widget, and adds it to the tree. */
	template< class T >
	T* ConstructWidget(TSubclassOf<UWidget> WidgetType)
	{
		// TODO UMG Editor only?
		Modify();

		// TODO UMG Don't have the widget tree responsible for construction and adding to the tree.

		UWidget* Widget = (UWidget*)ConstructObject<UWidget>(WidgetType, this);
		Widget->SetFlags(RF_Transactional);

		return (T*)Widget;
	}

	virtual void PreSave() override
	{
		AllWidgets.Empty();

		GetAllWidgets(AllWidgets);

		Super::PreSave();
	}

public:
	/** The root widget of the tree */
	UPROPERTY()
	UWidget* RootWidget;

protected:

	UPROPERTY()
	TArray< UWidget* > AllWidgets;
};

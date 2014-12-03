// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UserWidget.h"

#include "WidgetTree.generated.h"

/** The widget tree manages the collection of widgets in a blueprint widget. */
UCLASS()
class UMG_API UWidgetTree : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	/** Finds the widget in the tree by name. */
	UWidget* FindWidget(const FName& Name) const;

	/** Finds a widget in the tree using the native widget as the key. */
	UWidget* FindWidget(TSharedRef<SWidget> InWidget) const;

	/** Removes the widget from the hierarchy and all sub widgets. */
	bool RemoveWidget(UWidget* Widget);

	/** Gets the parent widget of a given widget, and potentially the child index. */
	class UPanelWidget* FindWidgetParent(UWidget* Widget, int32& OutChildIndex);

	/** Gathers all the widgets in the tree recursively */
	void GetAllWidgets(TArray<UWidget*>& Widgets) const;

	/** Gathers descendant child widgets of a parent widget. */
	void GetChildWidgets(UWidget* Parent, TArray<UWidget*>& Widgets) const;

	/**  */
	template <typename Predicate>
	FORCEINLINE void ForEachWidget(Predicate Pred) const
	{
		if ( RootWidget )
		{
			Pred(RootWidget);
			
			ForWidgetAndChildren(RootWidget, Pred);
		}
	}

	/**  */
	template <typename Predicate>
	FORCEINLINE void ForWidgetAndChildren(UWidget* Widget, Predicate Pred) const
	{
		if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Widget) )
		{
			TArray<FName> SlotNames;
			NamedSlotHost->GetSlotNames(SlotNames);

			for ( FName SlotName : SlotNames )
			{
				if ( UWidget* SlotContent = NamedSlotHost->GetContentForSlot(SlotName) )
				{
					Pred(SlotContent);

					ForWidgetAndChildren(SlotContent, Pred);
				}
			}
		}

		if ( UPanelWidget* PanelParent = Cast<UPanelWidget>(Widget) )
		{
			for ( int32 ChildIndex = 0; ChildIndex < PanelParent->GetChildrenCount(); ChildIndex++ )
			{
				if ( UWidget* ChildWidget = PanelParent->GetChildAt(ChildIndex) )
				{
					Pred(ChildWidget);

					ForWidgetAndChildren(ChildWidget, Pred);
				}
			}
		}
	}

	/** Constructs the widget, and adds it to the tree. */
	template< class T >
	FORCEINLINE T* ConstructWidget(TSubclassOf<UWidget> WidgetType)
	{
		if ( WidgetType->IsChildOf(UUserWidget::StaticClass()) )
		{
			UUserWidget* Widget = ConstructObject<UUserWidget>(WidgetType, this);
			Widget->Initialize();
			Widget->SetFlags(RF_Transactional);
			return (T*)Widget;
		}
		else
		{
			UWidget* Widget = (UWidget*)ConstructObject<UWidget>(WidgetType, this);
			Widget->SetFlags(RF_Transactional);
			return (T*)Widget;
		}
	}

	virtual void PreSave() override
	{
		AllWidgets.Empty();

		GetAllWidgets(AllWidgets);

		Super::PreSave();
	}

	virtual void PostLoad() override
	{
		//AllWidgets.Empty();

		Super::PostLoad();
	}

public:
	/** The root widget of the tree */
	UPROPERTY()
	UWidget* RootWidget;

protected:

	UPROPERTY()
	TArray< UWidget* > AllWidgets;
};

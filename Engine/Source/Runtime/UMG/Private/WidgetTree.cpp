// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

/////////////////////////////////////////////////////
// UWidgetTree

UWidgetTree::UWidgetTree(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
}

UWidget* UWidgetTree::FindWidget(const FString& Name) const
{
	FString ExistingName;

	// TODO UMG Hacky, remove this find widget function, or make it faster.
	TArray<UWidget*> Widgets;
	GetAllWidgets(Widgets);

	for ( UWidget* Widget : Widgets )
	{
		Widget->GetName(ExistingName);
		if ( ExistingName.Equals(Name, ESearchCase::IgnoreCase) )
		{
			return Widget;
		}
	}

	return NULL;
}

UWidget* UWidgetTree::FindWidget(TSharedRef<SWidget> InWidget) const
{
	FString ExistingName;

	// TODO UMG Hacky, remove this find widget function, or make it faster.
	TArray<UWidget*> Widgets;
	GetAllWidgets(Widgets);

	for ( UWidget* Widget : Widgets )
	{
		if ( Widget->GetCachedWidget() == InWidget )
		{
			return Widget;
		}
	}

	return NULL;
}

UPanelWidget* UWidgetTree::FindWidgetParent(UWidget* Widget, int32& OutChildIndex)
{
	UPanelWidget* Parent = Widget->GetParent();
	if ( Parent != NULL )
	{
		OutChildIndex = Parent->GetChildIndex(Widget);
	}
	else
	{
		OutChildIndex = 0;
	}

	return Parent;
}

bool UWidgetTree::RemoveWidget(UWidget* InRemovedWidget)
{
	bool bRemoved = false;

	UPanelWidget* InRemovedWidgetParent = InRemovedWidget->GetParent();
	if ( InRemovedWidgetParent )
	{
		if ( InRemovedWidgetParent->RemoveChild(InRemovedWidget) )
		{
			bRemoved = true;
		}
	}
	// If the widget being removed is the root, null it out.
	else if ( InRemovedWidget == RootWidget )
	{
		RootWidget = NULL;
		bRemoved = true;
	}

	return bRemoved;
}

void UWidgetTree::GetAllWidgets(TArray<UWidget*>& Widgets) const
{
	if ( RootWidget )
	{
		Widgets.Add(RootWidget);
		GetChildWidgets(RootWidget, Widgets);
	}
}

void UWidgetTree::GetChildWidgets(UWidget* Parent, TArray<UWidget*>& Widgets) const
{
	if ( UPanelWidget* PanelParent = Cast<UPanelWidget>(Parent) )
	{
		for ( int32 ChildIndex = 0; ChildIndex < PanelParent->GetChildrenCount(); ChildIndex++ )
		{
			if ( UWidget* ChildWidget = PanelParent->GetChildAt(ChildIndex) )
			{
				Widgets.Add(ChildWidget);

				GetChildWidgets(ChildWidget, Widgets);
			}
		}
	}
}

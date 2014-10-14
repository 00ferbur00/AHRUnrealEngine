// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UUniformGridPanel

UUniformGridPanel::UUniformGridPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsVariable = false;

	SUniformGridPanel::FArguments Defaults;
	Visiblity = UWidget::ConvertRuntimeToSerializedVisiblity(Defaults._Visibility.Get());
}

void UUniformGridPanel::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyUniformGridPanel.Reset();
}

UClass* UUniformGridPanel::GetSlotClass() const
{
	return UUniformGridSlot::StaticClass();
}

void UUniformGridPanel::OnSlotAdded(UPanelSlot* Slot)
{
	// Add the child to the live canvas if it already exists
	if ( MyUniformGridPanel.IsValid() )
	{
		Cast<UUniformGridSlot>(Slot)->BuildSlot(MyUniformGridPanel.ToSharedRef());
	}
}

void UUniformGridPanel::OnSlotRemoved(UPanelSlot* Slot)
{
	// Remove the widget from the live slot if it exists.
	if ( MyUniformGridPanel.IsValid() )
	{
		TSharedPtr<SWidget> Widget = Slot->Content->GetCachedWidget();
		if ( Widget.IsValid() )
		{
			MyUniformGridPanel->RemoveSlot(Widget.ToSharedRef());
		}
	}
}

TSharedRef<SWidget> UUniformGridPanel::RebuildWidget()
{
	MyUniformGridPanel =
		SNew(SUniformGridPanel)
		.MinDesiredSlotWidth(MinDesiredSlotWidth)
		.MinDesiredSlotHeight(MinDesiredSlotHeight);

	for ( UPanelSlot* Slot : Slots )
	{
		if ( UUniformGridSlot* TypedSlot = Cast<UUniformGridSlot>(Slot) )
		{
			TypedSlot->Parent = this;
			TypedSlot->BuildSlot(MyUniformGridPanel.ToSharedRef());
		}
	}

	return BuildDesignTimeWidget( MyUniformGridPanel.ToSharedRef() );
}

UUniformGridSlot* UUniformGridPanel::AddChildToUniformGrid(UWidget* Content)
{
	return Cast<UUniformGridSlot>(Super::AddChild(Content));
}

void UUniformGridPanel::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyUniformGridPanel->SetSlotPadding(SlotPadding);
}

#if WITH_EDITOR

const FSlateBrush* UUniformGridPanel::GetEditorIcon()
{
	return FUMGStyle::Get().GetBrush("Widget.UniformGrid");
}

const FText UUniformGridPanel::GetPaletteCategory()
{
	return LOCTEXT("Panel", "Panel");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE

// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#if WITH_EDITOR
#include "MessageLog.h"
#include "UObjectToken.h"
#endif

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UPanelWidget

UPanelWidget::UPanelWidget(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
	, bCanHaveMultipleChildren(true)
{
}

void UPanelWidget::ReleaseNativeWidget()
{
	Super::ReleaseNativeWidget();

	for ( int32 SlotIndex = 0; SlotIndex < Slots.Num(); SlotIndex++ )
	{
		if ( Slots[SlotIndex]->Content != NULL )
		{
			Slots[SlotIndex]->ReleaseNativeWidget();
		}
	}
}

int32 UPanelWidget::GetChildrenCount() const
{
	return Slots.Num();
}

UWidget* UPanelWidget::GetChildAt(int32 Index) const
{
	return Slots[Index]->Content;
}

int32 UPanelWidget::GetChildIndex(UWidget* Content) const
{
	const int32 ChildCount = GetChildrenCount();
	for ( int32 ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++ )
	{
		if ( GetChildAt(ChildIndex) == Content )
		{
			return ChildIndex;
		}
	}

	return -1;
}

bool UPanelWidget::RemoveChildAt(int32 Index)
{
	UPanelSlot* Slot = Slots[Index];
	if ( Slot->Content )
	{
		Slot->Content->Slot = NULL;
	}

	Slot->Parent = NULL;
	Slots.RemoveAt(Index);

	OnSlotRemoved(Slot);

	return true;
}

UPanelSlot* UPanelWidget::AddChild(UWidget* Content)
{
	if ( Content == nullptr )
	{
		return NULL;
	}

	if ( !bCanHaveMultipleChildren && GetChildrenCount() > 0 )
	{
		return NULL;
	}

	Content->RemoveFromParent();

	UPanelSlot* Slot = ConstructObject<UPanelSlot>(GetSlotClass(), this);
	Slot->SetFlags(RF_Transactional);
	Slot->Content = Content;
	Slot->Parent = this;

	if ( Content )
	{
		Content->Slot = Slot;
	}

	Slots.Add(Slot);

	OnSlotAdded(Slot);

	return Slot;
}

void UPanelWidget::ReplaceChildAt(int32 Index, UWidget* Content)
{
	UPanelSlot* Slot = Slots[Index];
	Slot->Content = Content;

	if ( Content )
	{
		Content->Slot = Slot;
	}

	Slot->SyncronizeProperties();
}

void UPanelWidget::InsertChildAt(int32 Index, UWidget* Content)
{
	UPanelSlot* Slot = ConstructObject<UPanelSlot>(GetSlotClass(), this);
	Slot->SetFlags(RF_Transactional);
	Slot->Content = Content;
	Slot->Parent = this;

	if ( Content )
	{
		Content->Slot = Slot;
	}

	Slots.Insert(Slot, Index);

	OnSlotAdded(Slot);
}

bool UPanelWidget::RemoveChild(UWidget* Content)
{
	int32 ChildIndex = GetChildIndex(Content);
	if ( ChildIndex != -1 )
	{
		return RemoveChildAt(ChildIndex);
	}

	return false;
}

void UPanelWidget::PostLoad()
{
	Super::PostLoad();

	for ( int32 SlotIndex = 0; SlotIndex < Slots.Num(); SlotIndex++ )
	{
		// Remove any slots where their content is null, we don't support content-less slots.
		if ( Slots[SlotIndex]->Content == NULL )
		{
			Slots.RemoveAt(SlotIndex);
			SlotIndex--;
		}
	}
}

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE

// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#if WITH_EDITOR
#include "MessageLog.h"
#include "UObjectToken.h"
#endif

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UPanelWidget

UPanelWidget::UPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bCanHaveMultipleChildren(true)
{
}

void UPanelWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	if ( bReleaseChildren )
	{
		for ( int32 SlotIndex = 0; SlotIndex < Slots.Num(); SlotIndex++ )
		{
			if ( Slots[SlotIndex]->Content != NULL )
			{
				Slots[SlotIndex]->ReleaseSlateResources(bReleaseChildren);
			}
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

	Slot->SynchronizeProperties();
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

	// Only allow inserting within the valid range of slots (and one more than the size).
	Index = FMath::Clamp(Index, 0, FMath::Max(Slots.Num(), 1));

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

bool UPanelWidget::HasAnyChildren() const
{
	return GetChildrenCount() > 0;
}

void UPanelWidget::ClearChildren()
{
	int32 Children = GetChildrenCount();
	for ( int32 ChildIndex = 0; ChildIndex < Children; ChildIndex++ )
	{
		RemoveChildAt(0);
	}
}

void UPanelWidget::SetIsDesignTime(bool bInDesignTime)
{
	Super::SetIsDesignTime(bInDesignTime);

	// Also mark all children as design time widgets.
	int32 Children = GetChildrenCount();
	for ( int32 SlotIndex = 0; SlotIndex < Children; SlotIndex++ )
	{
		if ( Slots[SlotIndex]->Content != NULL )
		{
			Slots[SlotIndex]->Content->SetIsDesignTime(bInDesignTime);
		}
	}
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

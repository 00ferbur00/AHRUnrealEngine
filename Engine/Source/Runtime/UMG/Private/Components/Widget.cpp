// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

/////////////////////////////////////////////////////
// UWidget

UWidget::UWidget(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	bIsEnabled = true;
	bIsVariable = true;
	bDesignTime = false;
	Visiblity = ESlateVisibility::Visible;
}

bool UWidget::GetIsEnabled() const
{
	return MyWidget.IsValid() ? MyWidget.Pin()->IsEnabled() : bIsEnabled;
}

void UWidget::SetIsEnabled(bool bInIsEnabled)
{
	bIsEnabled = bInIsEnabled;
	if ( MyWidget.IsValid() )
	{
		MyWidget.Pin()->SetEnabled(bInIsEnabled);
	}
}

TEnumAsByte<ESlateVisibility::Type> UWidget::GetVisibility()
{
	if ( MyWidget.IsValid() )
	{
		return UWidget::ConvertRuntimeToSerializedVisiblity(MyWidget.Pin()->GetVisibility());
	}

	return Visiblity;
}

void UWidget::SetVisibility(TEnumAsByte<ESlateVisibility::Type> InVisibility)
{
	Visiblity = InVisibility;

	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->SetVisibility(UWidget::ConvertSerializedVisibilityToRuntime(InVisibility));
	}
}

void UWidget::SetToolTipText(const FText& InToolTipText)
{
	ToolTipText = InToolTipText;

	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->SetToolTipText(InToolTipText);
	}
}

bool UWidget::IsHovered() const
{
	if ( MyWidget.IsValid() )
	{
		return MyWidget.Pin()->IsHovered();
	}

	return false;
}

void UWidget::ForceLayoutPrepass()
{
	if ( MyWidget.IsValid() )
	{
		MyWidget.Pin()->SlatePrepass();
	}
}

UPanelWidget* UWidget::GetParent() const
{
	if ( Slot )
	{
		return Slot->Parent;
	}

	return NULL;
}

TSharedRef<SWidget> UWidget::GetWidget() const
{
	TSharedPtr<SWidget> SafeWidget;

	UWidget* MutableThis = const_cast<UWidget*>( this );

	if ( !MyWidget.IsValid() )
	{
		// We lie a bit about this being a const function.  If this is the first call it's not really const
		// and we need to construct and cache the widget for the first run.  But instead of forcing everyone
		// downstream to make RebuildWidget const and force every implementation to make things mutable, we
		// just blow away the const here.
		SafeWidget = MutableThis->RebuildWidget();
		MyWidget = SafeWidget;
		MutableThis->SyncronizeProperties();
	}
	else
	{
		SafeWidget = MyWidget.Pin();
	}

	// If it is a user widget wrap it in a SObjectWidget to keep the instance from being GC'ed
	if ( MutableThis->IsA(UUserWidget::StaticClass()) )
	{
		return SNew(SObjectWidget, Cast<UUserWidget>(MutableThis))
		[
			SafeWidget.ToSharedRef()
		];
	}
	else
	{
		return SafeWidget.ToSharedRef();
	}
}

TSharedPtr<SWidget> UWidget::GetCachedWidget() const
{
	return MyWidget.Pin();
}

TSharedRef<SWidget> UWidget::BuildDesignTimeWidget(TSharedRef<SWidget> WrapWidget)
{
	if (IsDesignTime())
	{
		return SNew(SOverlay)
		
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			WrapWidget
		]
		
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.Visibility( EVisibility::HitTestInvisible )
			.BorderImage(FUMGStyle::Get().GetBrush("MarchingAnts"))
		];
	}
	else
	{
		return WrapWidget;
	}
}

#if WITH_EDITOR

bool UWidget::IsGeneratedName() const
{
	FString Name = GetName();
	FString BaseName = GetClass()->GetName() + "_";

	if ( Name.StartsWith(BaseName) )
	{
		return true;
	}

	return false;
}

FString UWidget::GetLabelMetadata() const
{
	return "";
}

FString UWidget::GetLabel() const
{
	if ( IsGeneratedName() && !bIsVariable )
	{
		return "[" + GetClass()->GetName() + "]" + GetLabelMetadata();
	}
	else
	{
		return GetName();
	}
}

const FSlateBrush* UWidget::GetEditorIcon()
{
	return FUMGStyle::Get().GetBrush("Widget");
}

TSharedRef<SWidget> UWidget::GetToolboxPreviewWidget() const
{
	return SNew(SImage);
	//.Image( FEditorStyle::GetBrush("UMGEditor.ToolboxPreviewWidget") );
}

void UWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if ( MyWidget.IsValid() )
	{
		SyncronizeProperties();
	}
}

void UWidget::Select()
{
	OnSelected();

	UWidget* Parent = GetParent();
	while ( Parent != NULL )
	{
		Parent->OnDescendantSelected(this);
		Parent = Parent->GetParent();
	}
}

void UWidget::Deselect()
{
	OnDeselected();

	UWidget* Parent = GetParent();
	while ( Parent != NULL )
	{
		Parent->OnDescendantDeselected(this);
		Parent = Parent->GetParent();
	}
}

#endif

bool UWidget::Modify(bool bAlwaysMarkDirty)
{
	bool Modified = Super::Modify(bAlwaysMarkDirty);

	if ( Slot )
		Modified &= Slot->Modify(bAlwaysMarkDirty);

	return Modified;
}

bool UWidget::IsChildOf(UWidget* PossibleParent)
{
	UPanelWidget* Parent = GetParent();
	if ( Parent == NULL )
	{
		return false;
	}
	else if ( Parent == PossibleParent )
	{
		return true;
	}
	
	return Parent->IsChildOf(PossibleParent);
}

TSharedRef<SWidget> UWidget::RebuildWidget()
{
	ensureMsg(false, TEXT("You must implement RebuildWidget() in your child class"));
	return SNew(SSpacer);
}

void UWidget::SyncronizeProperties()
{
	MyWidget.Pin()->SetEnabled(OPTIONAL_BINDING(bool, bIsEnabled));
	MyWidget.Pin()->SetVisibility(OPTIONAL_BINDING_CONVERT(ESlateVisibility::Type, Visiblity, EVisibility, ConvertVisibility));
	//MyWidget->SetCursor(OPTIONAL_BINDING(EMouseCursor)

	if ( !ToolTipText.IsEmpty() )
	{
		MyWidget.Pin()->SetToolTipText(OPTIONAL_BINDING(FText, ToolTipText));
	}
}

bool UWidget::IsDesignTime() const
{
	return bDesignTime;
}

void UWidget::IsDesignTime(bool bInDesignTime)
{
	bDesignTime = bInDesignTime;
}

EVisibility UWidget::ConvertSerializedVisibilityToRuntime(TEnumAsByte<ESlateVisibility::Type> Input)
{
	switch ( Input )
	{
	case ESlateVisibility::Visible:
		return EVisibility::Visible;
	case ESlateVisibility::Collapsed:
		return EVisibility::Collapsed;
	case ESlateVisibility::Hidden:
		return EVisibility::Hidden;
	case ESlateVisibility::HitTestInvisible:
		return EVisibility::HitTestInvisible;
	case ESlateVisibility::SelfHitTestInvisible:
		return EVisibility::SelfHitTestInvisible;
	default:
		check(false);
		return EVisibility::Visible;
	}
}

TEnumAsByte<ESlateVisibility::Type> UWidget::ConvertRuntimeToSerializedVisiblity(const EVisibility& Input)
{
	if ( Input == EVisibility::Visible )
	{
		return ESlateVisibility::Visible;
	}
	else if ( Input == EVisibility::Collapsed )
	{
		return ESlateVisibility::Collapsed;
	}
	else if ( Input == EVisibility::Hidden )
	{
		return ESlateVisibility::Hidden;
	}
	else if ( Input == EVisibility::HitTestInvisible )
	{
		return ESlateVisibility::HitTestInvisible;
	}
	else if ( Input == EVisibility::SelfHitTestInvisible )
	{
		return ESlateVisibility::SelfHitTestInvisible;
	}
	else
	{
		check(false);
		return ESlateVisibility::Visible;
	}
}

FSizeParam UWidget::ConvertSerializedSizeParamToRuntime(const FSlateChildSize& Input)
{
	switch ( Input.SizeRule )
	{
	default:
	case ESlateSizeRule::Automatic:
		return FAuto();
	case ESlateSizeRule::Fill:
		return FStretch(Input.Value);
	}
}

void UWidget::GatherChildren(UWidget* Root, TSet<UWidget*>& Children)
{
	UPanelWidget* PanelRoot = Cast<UPanelWidget>(Root);
	if ( PanelRoot )
	{
		for ( int32 ChildIndex = 0; ChildIndex < PanelRoot->GetChildrenCount(); ChildIndex++ )
		{
			UWidget* ChildWidget = PanelRoot->GetChildAt(ChildIndex);
			Children.Add(ChildWidget);
		}
	}
}

void UWidget::GatherAllChildren(UWidget* Root, TSet<UWidget*>& Children)
{
	UPanelWidget* PanelRoot = Cast<UPanelWidget>(Root);
	if ( PanelRoot )
	{
		for ( int32 ChildIndex = 0; ChildIndex < PanelRoot->GetChildrenCount(); ChildIndex++ )
		{
			UWidget* ChildWidget = PanelRoot->GetChildAt(ChildIndex);
			Children.Add(ChildWidget);

			GatherAllChildren(ChildWidget, Children);
		}
	}
}

UWidget* UWidget::FindChildContainingDescendant(UWidget* Root, UWidget* Descendant)
{
	UWidget* Parent = Descendant->GetParent();

	while ( Parent != NULL )
	{
		// If the Descendant's parent is the root, then the child containing the descendant is the descendant.
		if ( Parent == Root )
		{
			return Descendant;
		}

		Descendant = Parent;
		Parent = Parent->GetParent();
	}

	return NULL;
}

// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "SlatePrivatePCH.h"
#include "HittestGrid.h"

DECLARE_CYCLE_STAT( TEXT("OnPaint SViewport"), STAT_SlateOnPaint_SViewport, STATGROUP_Slate );

/* SViewport structors
 *****************************************************************************/

SViewport::SViewport()
	: bRenderDirectlyToWindow(false)
	, bEnableGammaCorrection(true)
{ }


/* SViewport interface
 *****************************************************************************/

void SViewport::Construct( const FArguments& InArgs )
{
	ShowDisabledEffect = InArgs._ShowEffectWhenDisabled;
	bRenderDirectlyToWindow = InArgs._RenderDirectlyToWindow;
	bEnableGammaCorrection = InArgs._EnableGammaCorrection;
	bEnableBlending = InArgs._EnableBlending;
	bIgnoreTextureAlpha = InArgs._IgnoreTextureAlpha;
	ViewportInterface = InArgs._ViewportInterface;
	ViewportSize = InArgs._ViewportSize;

	this->ChildSlot
	[
		InArgs._Content.Widget
	];
}


int32 SViewport::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
#if SLATE_HD_STATS
	SCOPE_CYCLE_COUNTER( STAT_SlateOnPaint_SViewport );
#endif
	bool bEnabled = ShouldBeEnabled( bParentEnabled );
	bool bShowDisabledEffect = ShowDisabledEffect.Get();
	ESlateDrawEffect::Type DrawEffects = bShowDisabledEffect && !bEnabled ? ESlateDrawEffect::DisabledEffect : ESlateDrawEffect::None;

	int32 LastHitTestIndex = Args.GetLastHitTestIndex();


	// Viewport texture alpha channels are often in an indeterminate state, even after the resolve,
	// so we'll tell the shader to not use the alpha channel when blending
	if( bIgnoreTextureAlpha )
	{
		DrawEffects |= ESlateDrawEffect::IgnoreTextureAlpha;
	}

	TSharedPtr<ISlateViewport> ViewportInterfacePin = ViewportInterface.Pin();

	// Tell the interface that we are drawing.
	if (ViewportInterfacePin.IsValid())
	{
		ViewportInterfacePin->OnDrawViewport( AllottedGeometry, MyClippingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled );
	}


	// Only draw a quad if not rendering directly to the backbuffer
	if( !ShouldRenderDirectly() )
	{
		if( ViewportInterfacePin.IsValid() && ViewportInterfacePin->GetViewportRenderTargetTexture() != nullptr )
		{
			FSlateDrawElement::MakeViewport( OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), ViewportInterfacePin, MyClippingRect, bEnableGammaCorrection, bEnableBlending, DrawEffects, InWidgetStyle.GetColorAndOpacityTint() );
		}
		else
		{
			// Viewport isn't ready yet, so just draw a black box
			static FSlateColorBrush BlackBrush( FColor::Black );
			FSlateDrawElement::MakeBox( OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &BlackBrush, MyClippingRect, DrawEffects, BlackBrush.GetTint( InWidgetStyle ) );
		}
	}

	int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled );

	if( ViewportInterfacePin.IsValid() && ViewportInterfacePin->IsSoftwareCursorVisible() )
	{
		const FVector2D CursorPosScreenSpace = FSlateApplication::Get().GetCursorPos();		
		// @todo Slate: why are we calling OnCursorQuery in here?
		FCursorReply Reply = ViewportInterfacePin->OnCursorQuery( AllottedGeometry,
			FPointerEvent(
				FSlateApplicationBase::CursorPointerIndex,
				CursorPosScreenSpace,
				CursorPosScreenSpace,
				FVector2D::ZeroVector,
				TSet<FKey>(),
				FModifierKeysState() )
		 );
		EMouseCursor::Type CursorType = Reply.GetCursor();

		const FSlateBrush* Brush = FCoreStyle::Get().GetBrush(TEXT("SoftwareCursor_Grab"));
		if( CursorType == EMouseCursor::CardinalCross )
		{
			Brush = FCoreStyle::Get().GetBrush(TEXT("SoftwareCursor_CardinalCross"));
		}

		LayerId++;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry( ViewportInterfacePin->GetSoftwareCursorPosition() - ( Brush->ImageSize / 2 ), Brush->ImageSize ),
			Brush,
			MyClippingRect
		);
	}


	if(CustomHitTestPath.IsValid())
	{
		Args.InsertCustomHitTestPath(CustomHitTestPath.ToSharedRef(), LastHitTestIndex);
	}


	return Layer;
}


void SViewport::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if(ViewportInterface.IsValid())
	{
		ViewportInterface.Pin()->Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}
}


/* SWidget interface
 *****************************************************************************/

FCursorReply SViewport::OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnCursorQuery(MyGeometry, CursorEvent) : FCursorReply::Unhandled();
}


FReply SViewport::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnMouseButtonDown(MyGeometry, MouseEvent) : FReply::Unhandled();
}


FReply SViewport::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnMouseButtonUp(MyGeometry, MouseEvent) : FReply::Unhandled();
}


void SViewport::OnMouseEnter( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (ViewportInterface.IsValid())
	{
		ViewportInterface.Pin()->OnMouseEnter(MyGeometry, MouseEvent);
	}
}


void SViewport::OnMouseLeave( const FPointerEvent& MouseEvent )
{
	SCompoundWidget::OnMouseLeave(MouseEvent);

	if (ViewportInterface.IsValid())
	{
		ViewportInterface.Pin()->OnMouseLeave(MouseEvent);
	}
}


FReply SViewport::OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnMouseMove(MyGeometry, MouseEvent) : FReply::Unhandled();
}


FReply SViewport::OnMouseWheel( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnMouseWheel(MyGeometry, MouseEvent) : FReply::Unhandled();
}


FReply SViewport::OnMouseButtonDoubleClick( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnMouseButtonDoubleClick(MyGeometry, MouseEvent) : FReply::Unhandled();
}


FReply SViewport::OnKeyDown( const FGeometry& MyGeometry, const FKeyboardEvent& KeyboardEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnKeyDown(MyGeometry, KeyboardEvent) : FReply::Unhandled();
}

 
FReply SViewport::OnKeyUp( const FGeometry& MyGeometry, const FKeyboardEvent& KeyboardEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnKeyUp(MyGeometry, KeyboardEvent) : FReply::Unhandled();
}


FReply SViewport::OnKeyChar( const FGeometry& MyGeometry, const FCharacterEvent& CharacterEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnKeyChar(MyGeometry, CharacterEvent) : FReply::Unhandled();
}


FReply SViewport::OnKeyboardFocusReceived( const FGeometry& MyGeometry, const FKeyboardFocusEvent& InKeyboardFocusEvent )
{
	if (WidgetToFocusOnActivate.IsValid())
	{
		return FReply::Handled().SetKeyboardFocus(WidgetToFocusOnActivate.Pin().ToSharedRef(), InKeyboardFocusEvent.GetCause());
	}

	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnKeyboardFocusReceived(InKeyboardFocusEvent) : FReply::Unhandled();
}


void SViewport::OnKeyboardFocusLost( const FKeyboardFocusEvent& InKeyboardFocusEvent )
{
	if (ViewportInterface.IsValid())
	{
		ViewportInterface.Pin()->OnKeyboardFocusLost(InKeyboardFocusEvent);
	}
}

void SViewport::SetContent( TSharedPtr<SWidget> InContent )
{
	ChildSlot
	[
		InContent.IsValid() ? InContent.ToSharedRef() : (TSharedRef<SWidget>)SNullWidget::NullWidget
	];
}

void SViewport::SetCustomHitTestPath( TSharedPtr<ICustomHitTestPath> InCustomHitTestPath )
{
	CustomHitTestPath = InCustomHitTestPath;
}

TSharedPtr<ICustomHitTestPath> SViewport::GetCustomHitTestPath()
{
	return CustomHitTestPath;
}

void SViewport::OnWindowClosed( const TSharedRef<SWindow>& WindowBeingClosed )
{
	if (ViewportInterface.IsValid())
	{
		ViewportInterface.Pin()->OnViewportClosed();
	}
}


FReply SViewport::OnControllerButtonPressed( const FGeometry& MyGeometry, const FControllerEvent& ControllerEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnControllerButtonPressed(MyGeometry, ControllerEvent) : FReply::Unhandled();
}


FReply SViewport::OnControllerButtonReleased( const FGeometry& MyGeometry, const FControllerEvent& ControllerEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnControllerButtonReleased(MyGeometry, ControllerEvent) : FReply::Unhandled();
}


FReply SViewport::OnControllerAnalogValueChanged( const FGeometry& MyGeometry, const FControllerEvent& ControllerEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnControllerAnalogValueChanged(MyGeometry, ControllerEvent) : FReply::Unhandled();
}


FReply SViewport::OnTouchStarted( const FGeometry& MyGeometry, const FPointerEvent& TouchEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnTouchStarted(MyGeometry, TouchEvent) : FReply::Unhandled();
}


FReply SViewport::OnTouchMoved( const FGeometry& MyGeometry, const FPointerEvent& TouchEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnTouchMoved(MyGeometry, TouchEvent) : FReply::Unhandled();
}


FReply SViewport::OnTouchEnded( const FGeometry& MyGeometry, const FPointerEvent& TouchEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnTouchEnded(MyGeometry, TouchEvent) : FReply::Unhandled();
}


FReply SViewport::OnTouchGesture( const FGeometry& MyGeometry, const FPointerEvent& GestureEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnTouchGesture(MyGeometry, GestureEvent) : FReply::Unhandled();
}


FReply SViewport::OnMotionDetected( const FGeometry& MyGeometry, const FMotionEvent& MotionEvent )
{
	return ViewportInterface.IsValid() ? ViewportInterface.Pin()->OnMotionDetected(MyGeometry, MotionEvent) : FReply::Unhandled();
}

void SViewport::OnFinishedPointerInput()
{
	TSharedPtr<ISlateViewport> PinnedInterface = ViewportInterface.Pin();
	if (PinnedInterface.IsValid())
	{
		PinnedInterface->OnFinishedPointerInput();
	}
}

void SViewport::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	SCompoundWidget::OnArrangeChildren(AllottedGeometry, ArrangedChildren);
	if( ArrangedChildren.Allows3DWidgets() && CustomHitTestPath.IsValid() )
	{
		CustomHitTestPath->ArrangeChildren( ArrangedChildren );
	}
}

TSharedPtr<FVirtualPointerPosition> SViewport::TranslateMouseCoordinateFor3DChild(const TSharedRef<SWidget>& ChildWidget, const FGeometry& MyGeometry, const FVector2D& ScreenSpaceMouseCoordinate, const FVector2D& LastScreenSpaceMouseCoordinate) const
{
	if( CustomHitTestPath.IsValid() )
	{
		return CustomHitTestPath->TranslateMouseCoordinateFor3DChild( ChildWidget, MyGeometry, ScreenSpaceMouseCoordinate, LastScreenSpaceMouseCoordinate );
	}

	return nullptr;
}
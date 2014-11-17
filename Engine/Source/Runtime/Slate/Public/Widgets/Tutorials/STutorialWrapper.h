// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

/** Delegate for a named widget being highlighted */
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnWidgetHighlight, const FName&);

class STutorialWrapper : public SBorder
{
public:
	SLATE_BEGIN_ARGS( STutorialWrapper )
		: _Content()
	{
		_Visibility = EVisibility::SelfHitTestInvisible;
	}

	/** Slot for the wrapped content (optional) */
	SLATE_DEFAULT_SLOT( FArguments, Content )

	SLATE_END_ARGS()

	SLATE_API void Construct(const FArguments& InArgs, const FName& Name);

	/** Begin SWidget interface */
	SLATE_API virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;
	SLATE_API virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	/** End SWidget interface */

	/** 
	 * Get the delegate fired when a named widget wants to draw its highlight
	 * @returns a reference to the delegate
	 */
	SLATE_API static FOnWidgetHighlight& OnWidgetHighlight();

private:

	/** Get the values that the animation drives */
	void GetAnimationValues(float& OutAlphaFactor, float& OutPulseFactor, FLinearColor& OutShadowTint, FLinearColor& OutBorderTint) const;

private:

	/** The name of the widget */
	FName Name;

	/** Flag for whether we are playing animation or not */
	bool bIsPlaying;

	/** Animation curves for displaying border */
	FCurveSequence BorderPulseAnimation;
	FCurveSequence BorderIntroAnimation;

	/** Geometry cached from Tick() */
	FGeometry CachedGeometry;

	/** The delegate fired when a named widget wants to draw its highlight */
	static FOnWidgetHighlight OnWidgetHighlightDelegate;
};

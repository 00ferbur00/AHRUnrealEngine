﻿// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "SlatePrivatePCH.h"
#include "SlateWordWrapper.h"


/**
 * Construct this widget
 *
 * @param	InArgs	The declaration data for this widget
 */
void STextBlock::Construct( const FArguments& InArgs )
{
	Text = InArgs._Text;

	TextStyle = InArgs._TextStyle;

	HighlightText = InArgs._HighlightText;
	WrapTextAt = InArgs._WrapTextAt;
	AutoWrapText = InArgs._AutoWrapText;
	MinDesiredWidth = InArgs._MinDesiredWidth;

	CachedWrapTextWidth = 0.0f;
	CachedAutoWrapTextWidth = 0.0f;

	Font = InArgs._Font;
	ColorAndOpacity = InArgs._ColorAndOpacity;
	ShadowOffset = InArgs._ShadowOffset;
	ShadowColorAndOpacity = InArgs._ShadowColorAndOpacity;
	HighlightColor = InArgs._HighlightColor;
	HighlightShape = InArgs._HighlightShape;

	OnDoubleClicked = InArgs._OnDoubleClicked;

	// Request text size be cached
	bRequestCache = true;
}

FSlateFontInfo STextBlock::GetFont() const
{
	return Font.IsSet() ? Font.Get() : TextStyle->Font;
}

FSlateColor STextBlock::GetColorAndOpacity() const
{
	return ColorAndOpacity.IsSet() ? ColorAndOpacity.Get() : TextStyle->ColorAndOpacity;
}

FVector2D STextBlock::GetShadowOffset() const
{
	return ShadowOffset.IsSet() ? ShadowOffset.Get() : TextStyle->ShadowOffset;
}

FLinearColor STextBlock::GetShadowColorAndOpacity() const
{
	return ShadowColorAndOpacity.IsSet() ? ShadowColorAndOpacity.Get() : TextStyle->ShadowColorAndOpacity;
}

FLinearColor STextBlock::GetHighlightColor() const
{
	return HighlightColor.IsSet() ? HighlightColor.Get() : TextStyle->HighlightColor;
}

const FSlateBrush* STextBlock::GetHighlightShape() const
{
	return HighlightShape.IsSet() ? HighlightShape.Get() : &TextStyle->HighlightShape;
}

void STextBlock::SetText( const TAttribute< FText >& InText )
{
	struct Local
	{
		static FString PassThroughAttribute( TAttribute< FText > TextAttribute )
		{
			return TextAttribute.Get( FText::GetEmpty() ).ToString();
		}
	};

	Text = TAttribute< FString >::Create(TAttribute<FString>::FGetter::CreateStatic( &Local::PassThroughAttribute, InText));
	bRequestCache = true;
}

int32 STextBlock::OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
#if SLATE_HD_STATS
	SCOPE_CYCLE_COUNTER( STAT_SlateOnPaint_STextBlock );
#endif

	const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FSlateRect ClippingRect = AllottedGeometry.GetClippingRect().IntersectionWith(MyClippingRect);

	bool bEnabled = ShouldBeEnabled( bParentEnabled );
	ESlateDrawEffect::Type DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	FLinearColor CurShadowColor = GetShadowColorAndOpacity();
	const FVector2D CurShadowOffset = GetShadowOffset();

	const bool ShouldDropShadow = CurShadowOffset.Size() > 0 && CurShadowColor.A > 0;

	FSlateFontInfo FontInfo = GetFont();

	// Perform text auto-wrapping if that was enabled.
	// Note: This is done here rather than in Tick(), because Tick() doesn't get called while resizing windows, but OnPaint() does
	if( AutoWrapText.Get() )
	{
		const float OldWrapTextAt = CachedAutoWrapTextWidth;
		const float NewWrapTextAt = AllottedGeometry.Size.X;
		if( OldWrapTextAt != NewWrapTextAt )
		{
			// Available space has changed, so make sure that we recompute wrapping
			bRequestCache = true;
			CachedAutoWrapTextWidth = NewWrapTextAt;
		}
	}

	// Draw the text highlight
	{
		const FString& StringToHighlight = HighlightText.Get().ToString();
		const int32 CurrentHighlightLength = StringToHighlight.Len();

		// Do we have text to highlight?
		if( CurrentHighlightLength > 0 )
		{
			// Check to see if the original string contains text which needs to be highlighted
			// We need to use the original as the wrapped string has had newlines added to it
			const int32 CurrentHighlightStart = CachedOriginalString.Find(StringToHighlight, ESearchCase::IgnoreCase);
			if ( INDEX_NONE != CurrentHighlightStart )
			{
				const int32 CurrentHighlightEnd = CurrentHighlightStart + CurrentHighlightLength;

				TSharedPtr<FSlateRenderer> SlateRenderer = FSlateApplication::Get().GetRenderer();
				const int32 LineHeight = FontMeasureService->GetMaxCharacterHeight(FontInfo);

				// We might have to highlight multiple lines if the range we've found spans multiple entries in CachedWrappedLineData
				for(auto It = CachedWrappedLineData.CreateConstIterator(); It; ++It)
				{
					const int32 LineStart	= It->Key;
					const int32 LineEnd		= It->Value;

					// We need to highlight this line if it started after a break from a previous line, or has highlighted characters before its end
					if(CurrentHighlightStart <= LineStart || CurrentHighlightStart <= LineEnd)
					{
						// Clamp the highlight indices to this line so we can measure the highlighted text for just this line
						const int32 LineHighlightStart  = FMath::Max(CurrentHighlightStart, LineStart);
						const int32 LineHighlightEnd	= FMath::Min(CurrentHighlightEnd, LineEnd);

						// Figure out screen space locations to start drawing the highlight rectangle
						const FString TextUpToHighlightStart = CachedOriginalString.Mid(LineStart, LineHighlightStart - LineStart);
						const FVector2D HighlightStartOffset(FontMeasureService->Measure(TextUpToHighlightStart, FontInfo).X, LineHeight * It.GetIndex());

						// Figure out the actual text being highlighted
						// Cannot just use the filter string; it might be different case and therefore different width characters
						const FString TextToHighlight = CachedOriginalString.Mid(LineHighlightStart, LineHighlightEnd - LineHighlightStart);
						const FVector2D HighlightSize = FontMeasureService->Measure(TextToHighlight, FontInfo);

						// Draw the actual highlight rectangle
						FSlateDrawElement::MakeBox(
							OutDrawElements,
							++LayerId,
							AllottedGeometry.ToPaintGeometry(HighlightStartOffset, HighlightSize),
							GetHighlightShape(),
							ClippingRect,
							DrawEffects,
							InWidgetStyle.GetColorAndOpacityTint() * GetHighlightColor()
							);
					}
				}
			}
		}
	}

	// Draw the optional shadow
	if ( ShouldDropShadow )
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry( CurShadowOffset, AllottedGeometry.Size ),
			CachedWrappedString,
			FontInfo,
			ClippingRect,
			DrawEffects,
			CurShadowColor * InWidgetStyle.GetColorAndOpacityTint()
		);
	}

	// Draw the text itself
	FSlateDrawElement::MakeText(
		OutDrawElements,
		++LayerId,
		AllottedGeometry.ToPaintGeometry(),
		CachedWrappedString,
		FontInfo,
		ClippingRect,
		DrawEffects,
		InWidgetStyle.GetColorAndOpacityTint() * GetColorAndOpacity().GetColor(InWidgetStyle)
	);

	return LayerId;
}

/**
 * See SWidget::OnMouseButtonDoubleClick.
 *
 * @param MyGeometry The Geometry of the widget receiving the event
 * @param MouseEvent Information about the input event
 *
 * @return Whether the event was handled along with possible requests for the system to take action.
 */
FReply STextBlock::OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent )
{
	if ( InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		if( OnDoubleClicked.IsBound() )
		{
			OnDoubleClicked.Execute();

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void STextBlock::CacheDesiredSize()
{
#if SLATE_HD_STATS
	SCOPE_CYCLE_COUNTER( STAT_SlateCacheDesiredSize_STextBlock );
#endif

	// Get the wrapping width and font to see if they have changed
	float WrappingWidth = WrapTextAt.Get();
	const FSlateFontInfo& FontInfo = GetFont();

	// Text wrapping can either be used defined (WrapTextAt), automatic (AutoWrapText and CachedAutoWrapTextWidth), or a mixture of both
	// Take whichever has the smallest value (>1)
	if(AutoWrapText.Get() && CachedAutoWrapTextWidth >= 1.0f)
	{
		WrappingWidth = (WrappingWidth >= 1.0f) ? FMath::Min(WrappingWidth, CachedAutoWrapTextWidth) : CachedAutoWrapTextWidth;
	}

	// We should recache text size, width, font, and wrapped string if...
	// A cache was requested, the text is dynamic, the wrapping width has changed, or the font has changed
	const bool bShouldCacheText = bRequestCache || Text.IsBound() || (WrappingWidth != CachedWrapTextWidth) || (!(FontInfo == CachedFont));

	if(bShouldCacheText)
	{
		CachedOriginalString = Text.Get();

		const bool bWrapText = (WrappingWidth >= 1.0f);

		// Handle optional text wrapping, caching the appropriate result
		if(bWrapText)
		{
			// WrapText takes care of clearing CachedWrappedLineData
			CachedWrappedString = SlateWordWrapper::WrapText(CachedOriginalString, FontInfo, FMath::TruncToInt(WrappingWidth), 1.0f, &CachedWrappedLineData);
		}
		else
		{
			CachedWrappedString = CachedOriginalString;

			CachedWrappedLineData.Empty();
			CachedWrappedLineData.Add(TPairInitializer<int32, int32>(0, CachedOriginalString.Len()));
		}

		const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D TextMeasurement = FontMeasureService->Measure(CachedWrappedString, FontInfo);

		// Force the result to exactly the wrapping width
		if(bWrapText)
		{
			TextMeasurement.X = FMath::Min(TextMeasurement.X, WrappingWidth);
		}

		TextMeasurement.X = FMath::Max(TextMeasurement.X, MinDesiredWidth.Get());

		const FVector2D CurrentShadowOffset = GetShadowOffset();
		const FVector2D AbsoluteShadowOffset(FMath::Abs(CurrentShadowOffset.X), FMath::Abs(CurrentShadowOffset.Y));
		this->Advanced_SetDesiredSize(TextMeasurement + AbsoluteShadowOffset);

		// Update cached values
		CachedWrapTextWidth = WrappingWidth;
		CachedFont = FontInfo;
		bRequestCache = false;
	}
}

FVector2D STextBlock::ComputeDesiredSize() const
{
	// Usually widgets just override ComputeDesiredSize(), but STextBlock
	// overrides CacheDesiredSize() and does all the work in there.

	// Dummy implementation.
	return FVector2D::ZeroVector;
}

void STextBlock::SetFont(const TAttribute< FSlateFontInfo >& InFont)
{
	Font = InFont;
	bRequestCache = true;
}

void STextBlock::SetColorAndOpacity(const TAttribute<FSlateColor>& InColorAndOpacity)
{
	ColorAndOpacity = InColorAndOpacity;
}

void STextBlock::SetTextStyle(const FTextBlockStyle* InTextStyle)
{
	TextStyle = InTextStyle;
	bRequestCache = true;
}

void STextBlock::SetWrapTextAt(const TAttribute<float>& InWrapTextAt)
{
	WrapTextAt = InWrapTextAt;
}

void STextBlock::SetAutoWrapText(const TAttribute<bool>& InAutoWrapText)
{
	AutoWrapText = InAutoWrapText;
}

void STextBlock::SetShadowOffset(const TAttribute<FVector2D>& InShadowOffset)
{
	ShadowOffset = InShadowOffset;
}

void STextBlock::SetShadowColorAndOpacity(const TAttribute<FLinearColor>& InShadowColorAndOpacity)
{
	ShadowColorAndOpacity = InShadowColorAndOpacity;
}

void STextBlock::SetMinDesiredWidth(const TAttribute<float>& InMinDesiredWidth)
{
	MinDesiredWidth = InMinDesiredWidth;
}

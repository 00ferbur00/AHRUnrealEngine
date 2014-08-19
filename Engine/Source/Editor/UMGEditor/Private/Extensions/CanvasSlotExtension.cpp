// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"

#define LOCTEXT_NAMESPACE "UMG"

const float SnapDistance = 7;

static float DistancePointToLine2D(const FVector2D& LinePointA, const FVector2D& LinePointB, const FVector2D& PointC)
{
	FVector2D AB = LinePointB - LinePointA;
	FVector2D AC = PointC - LinePointA;

	float Distance = FVector2D::CrossProduct(AB, AC) / FVector2D::Distance(LinePointA, LinePointB);
	return FMath::Abs(Distance);
}

FCanvasSlotExtension::FCanvasSlotExtension()
	: bDragging(false)
{
	ExtensionId = FName(TEXT("CanvasSlot"));
}

bool FCanvasSlotExtension::CanExtendSelection(const TArray< FWidgetReference >& Selection) const
{
	for ( const FWidgetReference& Widget : Selection )
	{
		if ( !Widget.GetTemplate()->Slot || !Widget.GetTemplate()->Slot->IsA(UCanvasPanelSlot::StaticClass()) )
		{
			return false;
		}
	}

	return Selection.Num() == 1;
}

void FCanvasSlotExtension::ExtendSelection(const TArray< FWidgetReference >& Selection, TArray< TSharedRef<FDesignerSurfaceElement> >& SurfaceElements)
{
	SelectionCache = Selection;

	MoveHandle =
		SNew(SBorder)
		//.BorderImage(FEditorStyle::Get().GetBrush("ToolBar.Background"))
		//.BorderBackgroundColor(FLinearColor(0,0,0))
		.OnMouseButtonDown(this, &FCanvasSlotExtension::HandleBeginDrag)
		.OnMouseButtonUp(this, &FCanvasSlotExtension::HandleEndDrag)
		.OnMouseMove(this, &FCanvasSlotExtension::HandleDragging)
		.Padding(FMargin(0))
		[
			SNew(SImage)
			.Image(FCoreStyle::Get().GetBrush("SoftwareCursor_CardinalCross"))
		];

	MoveHandle->SlatePrepass();

	SurfaceElements.Add(MakeShareable(new FDesignerSurfaceElement(MoveHandle.ToSharedRef(), EExtensionLayoutLocation::TopLeft, FVector2D(0, -(MoveHandle->GetDesiredSize().Y + 10)))));
}

bool FCanvasSlotExtension::GetCollisionSegmentsForSlot(UCanvasPanel* Canvas, int32 SlotIndex, TArray<FVector2D>& Segments)
{
	FGeometry ArrangedGeometry;
	if ( Canvas->GetGeometryForSlot(SlotIndex, ArrangedGeometry) )
	{
		GetCollisionSegmentsFromGeometry(ArrangedGeometry, Segments);
		return true;
	}
	return false;
}

bool FCanvasSlotExtension::GetCollisionSegmentsForSlot(UCanvasPanel* Canvas, UCanvasPanelSlot* Slot, TArray<FVector2D>& Segments)
{
	FGeometry ArrangedGeometry;
	if ( Canvas->GetGeometryForSlot(Slot, ArrangedGeometry) )
	{
		GetCollisionSegmentsFromGeometry(ArrangedGeometry, Segments);
		return true;
	}
	return false;
}

void FCanvasSlotExtension::GetCollisionSegmentsFromGeometry(FGeometry ArrangedGeometry, TArray<FVector2D>& Segments)
{
	Segments.SetNumUninitialized(8);

	// Left Side
	Segments[0] = ArrangedGeometry.Position;
	Segments[1] = ArrangedGeometry.Position + FVector2D(0, ArrangedGeometry.Size.Y);

	// Top Side
	Segments[2] = ArrangedGeometry.Position;
	Segments[3] = ArrangedGeometry.Position + FVector2D(ArrangedGeometry.Size.X, 0);

	// Right Side
	Segments[4] = ArrangedGeometry.Position + FVector2D(ArrangedGeometry.Size.X, 0);
	Segments[5] = ArrangedGeometry.Position + ArrangedGeometry.Size;

	// Bottom Side
	Segments[6] = ArrangedGeometry.Position + FVector2D(0, ArrangedGeometry.Size.Y);
	Segments[7] = ArrangedGeometry.Position + ArrangedGeometry.Size;
}

void FCanvasSlotExtension::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{

}

void FCanvasSlotExtension::Paint(const TSet< FWidgetReference >& Selection, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	PaintCollisionLines(Selection, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

FReply FCanvasSlotExtension::HandleBeginDrag(const FGeometry& Geometry, const FPointerEvent& Event)
{
	bDragging = true;

	BeginTransaction(LOCTEXT("MoveWidget", "Move Widget"));

	return FReply::Handled().CaptureMouse(MoveHandle.ToSharedRef());
}

FReply FCanvasSlotExtension::HandleEndDrag(const FGeometry& Geometry, const FPointerEvent& Event)
{
	bDragging = false;

	EndTransaction();

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply FCanvasSlotExtension::HandleDragging(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if ( bDragging )
	{
		float InverseScale = ( 1.0f / Designer->GetPreviewScale() );

		for ( FWidgetReference& Selection : SelectionCache )
		{
			MoveByAmount(Selection, Event.GetCursorDelta() * InverseScale);
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void FCanvasSlotExtension::MoveByAmount(FWidgetReference& WidgetRef, FVector2D Delta)
{
	if ( Delta.IsZero() )
	{
		return;
	}

	UWidget* Widget = WidgetRef.GetPreview();

	UCanvasPanelSlot* CanvasSlot = CastChecked<UCanvasPanelSlot>(Widget->Slot);
	UCanvasPanel* Parent = CastChecked<UCanvasPanel>(CanvasSlot->Parent);

	FMargin Offsets = CanvasSlot->LayoutData.Offsets;
	Offsets.Left += Delta.X;
	Offsets.Top += Delta.Y;

	// If the slot is stretched horizontally we need to move the right side as it no longer represents width, but
	// now represents margin from the right stretched side.
	if ( CanvasSlot->LayoutData.Anchors.IsStretchedHorizontal() )
	{
		Offsets.Right -= Delta.X;
	}

	// If the slot is stretched vertically we need to move the bottom side as it no longer represents width, but
	// now represents margin from the bottom stretched side.
	if ( CanvasSlot->LayoutData.Anchors.IsStretchedVertical() )
	{
		Offsets.Bottom -= Delta.Y;
	}

	CanvasSlot->SetOffsets(Offsets);

	// Update the Template widget to match
	UWidget* TemplateWidget = WidgetRef.GetTemplate();
	UCanvasPanelSlot* TemplateSlot = CastChecked<UCanvasPanelSlot>(TemplateWidget->Slot);

	TemplateSlot->SetOffsets(Offsets);
}

void FCanvasSlotExtension::PaintCollisionLines(const TSet< FWidgetReference >& Selection, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	for ( const FWidgetReference& WidgetRef : Selection )
	{
		if ( !WidgetRef.IsValid() )
		{
			continue;
		}

		UWidget* Widget = WidgetRef.GetPreview();

		if ( UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot) )
		{
			if ( UCanvasPanel* Canvas = CastChecked<UCanvasPanel>(CanvasSlot->Parent) )
			{
				//TODO UMG Only show guide lines when near them and dragging
				if ( bDragging )
				{
					FGeometry MyArrangedGeometry;
					if ( !Canvas->GetGeometryForSlot(CanvasSlot, MyArrangedGeometry) )
					{
						continue;
					}

					TArray<FVector2D> LinePoints;
					LinePoints.AddUninitialized(2);

					// Get the collision segments that we could potentially be docking against.
					TArray<FVector2D> MySegments;
					if ( GetCollisionSegmentsForSlot(Canvas, CanvasSlot, MySegments) )
					{
						for ( int32 MySegmentIndex = 0; MySegmentIndex < MySegments.Num(); MySegmentIndex += 2 )
						{
							FVector2D MySegmentBase = MySegments[MySegmentIndex];

							for ( int32 SlotIndex = 0; SlotIndex < Canvas->GetChildrenCount(); SlotIndex++ )
							{
								// Ignore the slot being dragged.
								if ( Canvas->Slots[SlotIndex] == CanvasSlot )
								{
									continue;
								}

								// Get the collision segments that we could potentially be docking against.
								TArray<FVector2D> Segments;
								if ( GetCollisionSegmentsForSlot(Canvas, SlotIndex, Segments) )
								{
									for ( int32 SegmentBase = 0; SegmentBase < Segments.Num(); SegmentBase += 2 )
									{
										FVector2D PointA = Segments[SegmentBase];
										FVector2D PointB = Segments[SegmentBase + 1];

										FVector2D CollisionPoint = MySegmentBase;

										//TODO Collide against all sides of the arranged geometry.
										float Distance = DistancePointToLine2D(PointA, PointB, CollisionPoint);
										if ( Distance <= SnapDistance )
										{
											FVector2D FarthestPoint = FVector2D::Distance(PointA, CollisionPoint) > FVector2D::Distance(PointB, CollisionPoint) ? PointA : PointB;
											FVector2D NearestPoint = FVector2D::Distance(PointA, CollisionPoint) > FVector2D::Distance(PointB, CollisionPoint) ? PointB : PointA;

											LinePoints[0] = FarthestPoint;
											LinePoints[1] = FarthestPoint + ( NearestPoint - FarthestPoint ) * 100000;

											LinePoints[0].X = FMath::Clamp(LinePoints[0].X, 0.0f, MyClippingRect.Right - MyClippingRect.Left);
											LinePoints[0].Y = FMath::Clamp(LinePoints[0].Y, 0.0f, MyClippingRect.Bottom - MyClippingRect.Top);

											LinePoints[1].X = FMath::Clamp(LinePoints[1].X, 0.0f, MyClippingRect.Right - MyClippingRect.Left);
											LinePoints[1].Y = FMath::Clamp(LinePoints[1].Y, 0.0f, MyClippingRect.Bottom - MyClippingRect.Top);

											FLinearColor Color(0.5f, 0.75, 1);
											const bool bAntialias = true;

											FSlateDrawElement::MakeLines(
												OutDrawElements,
												LayerId,
												AllottedGeometry.ToPaintGeometry(),
												LinePoints,
												MyClippingRect,
												ESlateDrawEffect::None,
												Color,
												bAntialias);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

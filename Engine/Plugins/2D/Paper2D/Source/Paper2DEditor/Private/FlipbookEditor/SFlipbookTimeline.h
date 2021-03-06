// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

struct FSpriteAnimationFrame;

// Called when the selection changes
DECLARE_DELEGATE_OneParam(FOnFlipbookKeyframeSelectionChanged, int32);

class SFlipbookTimeline : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFlipbookTimeline)
		: _FlipbookBeingEdited(nullptr)
		, _PlayTime(0)
	{}

		SLATE_ATTRIBUTE(class UPaperFlipbook*, FlipbookBeingEdited)
		SLATE_ATTRIBUTE(float, PlayTime)
		SLATE_EVENT(FOnFlipbookKeyframeSelectionChanged, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<const FUICommandList> InCommandList);

	// SWidget interface
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// End of SWidget interface

private:
	void OnAssetsDropped(const class FAssetDragDropOp& DragDropOp);

	TSharedRef<SWidget> GenerateContextMenu();
	EVisibility NoFramesWarningVisibility() const;
private:
	TAttribute<class UPaperFlipbook*> FlipbookBeingEdited;
	TAttribute<float> PlayTime;
	TSharedPtr<const FUICommandList> CommandList;
	FOnFlipbookKeyframeSelectionChanged OnSelectionChanged;
	int32 SlateUnitsPerFrame;
};

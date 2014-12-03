// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "SlateReflectorPrivatePCH.h"


#define LOCTEXT_NAMESPACE "SWidgetReflector"
#define WITH_EVENT_LOGGING 0

static const int32 MaxLoggedEvents = 100;


/* Local helpers
 *****************************************************************************/

struct FLoggedEvent
{
	FLoggedEvent( const FInputEvent& InEvent, const FReplyBase& InReply )
		: Event(InEvent)
		, Handler(InReply.GetHandler())
		, EventText(InEvent.ToText())
		, HandlerText(InReply.GetHandler().IsValid() ? FText::FromString(InReply.GetHandler()->ToString()) : LOCTEXT("NullHandler", "null"))
	{ }

	FText ToText()
	{
		return FText::Format(NSLOCTEXT("","","{0}  |  {1}"), EventText, HandlerText);
	}
	
	FInputEvent Event;
	TWeakPtr<SWidget> Handler;
	FText EventText;
	FText HandlerText;
};


/* SWidgetReflector interface
 *****************************************************************************/

void SWidgetReflector::Construct( const FArguments& InArgs )
{
	LoggedEvents.Reserve(MaxLoggedEvents);

	bShowFocus = false;
	bIsPicking = false;

	this->ChildSlot
	[
		SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				
				+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
					
						+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
									.Text(LOCTEXT("AppScale", "Application Scale: ").ToString())
							]

						+ SHorizontalBox::Slot()
							.MaxWidth(250)
							[
								SNew(SSpinBox<float>)
									.Value(this, &SWidgetReflector::HandleAppScaleSliderValue)
									.MinValue(0.1f)
									.MaxValue(3.0f)
									.Delta(0.01f)
									.OnValueChanged(this, &SWidgetReflector::HandleAppScaleSliderChanged)
							]
					]

				+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(5.0f)
							[
								// Check box that controls LIVE MODE
								SNew(SCheckBox)
									.IsChecked(this, &SWidgetReflector::HandleFocusCheckBoxIsChecked)
									.OnCheckStateChanged(this, &SWidgetReflector::HandleFocusCheckBoxCheckedStateChanged)
									[
										SNew(STextBlock)
											.Text(LOCTEXT("ShowFocus", "Show Focus").ToString())
									]
							]

						+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(5.0f)
							[
								// Check box that controls PICKING A WIDGET TO INSPECT
								SNew(SButton)
									.OnClicked(this, &SWidgetReflector::HandlePickButtonClicked)
									.ButtonColorAndOpacity(this, &SWidgetReflector::HandlePickButtonColorAndOpacity)
									[
										SNew(STextBlock)
											.Text(this, &SWidgetReflector::HandlePickButtonText)
									]
							]
					]

				+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						// The tree view that shows all the info that we capture.
						SAssignNew(ReflectorTree, SReflectorTree)
							.ItemHeight(24.0f)
							.TreeItemsSource(&ReflectorTreeRoot)
							.OnGenerateRow(this, &SWidgetReflector::HandleReflectorTreeGenerateRow)
							.OnGetChildren(this, &SWidgetReflector::HandleReflectorTreeGetChildren)
							.OnSelectionChanged(this, &SWidgetReflector::HandleReflectorTreeSelectionChanged)
							.HeaderRow
							(
								SNew(SHeaderRow)

								+ SHeaderRow::Column("WidgetName")
									.DefaultLabel(LOCTEXT("WidgetName", "Widget Name").ToString())
									.FillWidth(0.65f)

								+ SHeaderRow::Column("ForegroundColor")
									.FixedWidth(24.0f)
									.VAlignHeader(VAlign_Center)
									.HeaderContent()
									[
										SNew(STextBlock)
											.Text(LOCTEXT("ForegroundColor", "FG").ToString())
											.ToolTipText(LOCTEXT("ForegroundColorToolTip", "Foreground Color").ToString())
									]

								+ SHeaderRow::Column("Visibility")
									.DefaultLabel(LOCTEXT("Visibility", "Visibility" ).ToString())
									.FixedWidth(125.0f)

								+ SHeaderRow::Column("WidgetInfo")
									.DefaultLabel(LOCTEXT("WidgetInfo", "Widget Info" ).ToString())
									.FillWidth(0.25f)

								+ SHeaderRow::Column("Address")
									.DefaultLabel( LOCTEXT("Address", "Address") )
									.FillWidth( 0.10f )
							)
					]

				#if WITH_EVENT_LOGGING
				+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(EventListView, SListView<TSharedRef<FLoggedEvent>>)
							.ListItemsSource( &LoggedEvents )
							.OnGenerateRow(this, &SWidgetReflector::GenerateEventLogRow)
					]
				#endif //WITH_EVENT_LOGGING
			
				+ SVerticalBox::Slot()
					.AutoHeight()
					[
						// Frame rate counter
						SNew(STextBlock)
							.Text(this, &SWidgetReflector::HandleFrameRateText)
					]
			]
	];
}


/* SCompoundWidget overrides
 *****************************************************************************/

void SWidgetReflector::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}


void SWidgetReflector::OnEventProcessed( const FInputEvent& Event, const FReplyBase& InReply )
{
	#if WITH_EVENT_LOGGING
		if (LoggedEvents.Num() >= MaxLoggedEvents)
		{
			LoggedEvents.Empty();
		}

		LoggedEvents.Add(MakeShareable(new FLoggedEvent(Event, InReply)));
		EventListView->RequestListRefresh();
		EventListView->RequestScrollIntoView(LoggedEvents.Last());
	#endif //WITH_EVENT_LOGGING
}


/* IWidgetReflector overrides
 *****************************************************************************/

bool SWidgetReflector::ReflectorNeedsToDrawIn( TSharedRef<SWindow> ThisWindow ) const
{
	return ((SelectedNodes.Num() > 0) && (ReflectorTreeRoot.Num() > 0) && (ReflectorTreeRoot[0]->Widget.Pin() == ThisWindow));
}


void SWidgetReflector::SetWidgetsToVisualize( const FWidgetPath& InWidgetsToVisualize )
{
	ReflectorTreeRoot.Empty();

	if (InWidgetsToVisualize.IsValid())
	{
		ReflectorTreeRoot.Add(FReflectorNode::NewTreeFrom(InWidgetsToVisualize.Widgets[0]));
		PickedPath.Empty();

		FReflectorNode::FindWidgetPath( ReflectorTreeRoot, InWidgetsToVisualize, PickedPath );
		VisualizeAsTree(PickedPath);
	}
}


int32 SWidgetReflector::Visualize( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId )
{
	const bool bAttemptingToVisualizeReflector = InWidgetsToVisualize.ContainsWidget(SharedThis(this));

	if (!InWidgetsToVisualize.IsValid())
	{
		TSharedPtr<SWidget> WindowWidget = ReflectorTreeRoot[0]->Widget.Pin();
		TSharedPtr<SWindow> Window = StaticCastSharedPtr<SWindow>(WindowWidget);

		return VisualizeSelectedNodesAsRectangles(SelectedNodes, Window.ToSharedRef(), OutDrawElements, LayerId);
	}

	if (!bAttemptingToVisualizeReflector)
	{
		SetWidgetsToVisualize(InWidgetsToVisualize);

		return VisualizePickAsRectangles(InWidgetsToVisualize, OutDrawElements, LayerId);
	}		

	return LayerId;
}


/* SWidgetReflector implementation
 *****************************************************************************/

TSharedRef<SToolTip> SWidgetReflector::GenerateToolTipForReflectorNode( TSharedPtr<FReflectorNode> InReflectorNode )
{
	return SNew(SToolTip)
		[
			SNew(SReflectorToolTipWidget)
				.WidgetInfoToVisualize(InReflectorNode)
		];
}


void SWidgetReflector::VisualizeAsTree( const TArray<TSharedPtr<FReflectorNode>>& WidgetPathToVisualize )
{
	const FLinearColor TopmostWidgetColor(1.0f, 0.0f, 0.0f);
	const FLinearColor LeafmostWidgetColor(0.0f, 1.0f, 0.0f);

	for (int32 WidgetIndex = 0; WidgetIndex<WidgetPathToVisualize.Num(); ++WidgetIndex)
	{
		TSharedPtr<FReflectorNode> CurWidget = WidgetPathToVisualize[WidgetIndex];

		// Tint the item based on depth in picked path
		const float ColorFactor = static_cast<float>(WidgetIndex)/WidgetPathToVisualize.Num();
		CurWidget->Tint = FMath::Lerp(TopmostWidgetColor, LeafmostWidgetColor, ColorFactor);

		// Make sure the user can see the picked path in the tree.
		ReflectorTree->SetItemExpansion(CurWidget, true);
	}

	ReflectorTree->RequestScrollIntoView(WidgetPathToVisualize.Last());
	ReflectorTree->SetSelection(WidgetPathToVisualize.Last());
}


int32 SWidgetReflector::VisualizePickAsRectangles( const FWidgetPath& InWidgetsToVisualize, FSlateWindowElementList& OutDrawElements, int32 LayerId)
{
	const FLinearColor TopmostWidgetColor(1.0f, 0.0f, 0.0f);
	const FLinearColor LeafmostWidgetColor(0.0f, 1.0f, 0.0f);

	for (int32 WidgetIndex = 0; WidgetIndex < InWidgetsToVisualize.Widgets.Num(); ++WidgetIndex)
	{
		const FArrangedWidget& WidgetGeometry = InWidgetsToVisualize.Widgets[WidgetIndex];
		const float ColorFactor = static_cast<float>(WidgetIndex)/InWidgetsToVisualize.Widgets.Num();
		const FLinearColor Tint(1.0f - ColorFactor, ColorFactor, 0.0f, 1.0f);

		// The FGeometry we get is from a WidgetPath, so it's rooted in desktop space.
		// We need to APPEND a transform to the Geometry to essentially undo this root transform
		// and get us back into Window Space.
		// This is nonstandard so we have to go through some hoops and a specially exposed method 
		// in FPaintGeometry to allow appending layout transforms.
		FPaintGeometry WindowSpaceGeometry = WidgetGeometry.Geometry.ToPaintGeometry();
		WindowSpaceGeometry.AppendTransform(TransformCast<FSlateLayoutTransform>(Inverse(InWidgetsToVisualize.TopLevelWindow->GetPositionInScreen())));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++LayerId,
			WindowSpaceGeometry,
			FCoreStyle::Get().GetBrush(TEXT("Debug.Border")),
			InWidgetsToVisualize.TopLevelWindow->GetClippingRectangleInWindow(),
			ESlateDrawEffect::None,
			FMath::Lerp(TopmostWidgetColor, LeafmostWidgetColor, ColorFactor)
		);
	}

	return LayerId;
}


int32 SWidgetReflector::VisualizeSelectedNodesAsRectangles( const TArray<TSharedPtr<FReflectorNode>>& InNodesToDraw, const TSharedRef<SWindow>& VisualizeInWindow, FSlateWindowElementList& OutDrawElements, int32 LayerId )
{
	for (int32 NodeIndex = 0; NodeIndex < InNodesToDraw.Num(); ++NodeIndex)
	{
		const TSharedPtr<FReflectorNode>& NodeToDraw = InNodesToDraw[NodeIndex];
		const FLinearColor Tint(0.0f, 1.0f, 0.0f);

		// The FGeometry we get is from a WidgetPath, so it's rooted in desktop space.
		// We need to APPEND a transform to the Geometry to essentially undo this root transform
		// and get us back into Window Space.
		// This is nonstandard so we have to go through some hoops and a specially exposed method 
		// in FPaintGeometry to allow appending layout transforms.
		FPaintGeometry WindowSpaceGeometry = NodeToDraw->Geometry.ToPaintGeometry();
		WindowSpaceGeometry.AppendTransform(TransformCast<FSlateLayoutTransform>(Inverse(VisualizeInWindow->GetPositionInScreen())));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++LayerId,
			WindowSpaceGeometry,
			FCoreStyle::Get().GetBrush(TEXT("Debug.Border")),
			VisualizeInWindow->GetClippingRectangleInWindow(),
			ESlateDrawEffect::None,
			NodeToDraw->Tint
		);
	}

	return LayerId;
}


/* SWidgetReflector callbacks
 *****************************************************************************/

void SWidgetReflector::HandleFocusCheckBoxCheckedStateChanged( ESlateCheckBoxState::Type NewValue )
{
	bShowFocus = NewValue != ESlateCheckBoxState::Unchecked;

	if (bShowFocus)
	{
		bIsPicking = false;
	}
}


FString SWidgetReflector::HandleFrameRateText() const
{
	FString MyString;
#if 0 // the new stats system does not support this
	MyString = FString::Printf(TEXT("FPS: %0.2f (%0.2f ms)"), (float)( 1.0f / FPSCounter.GetAverage()), (float)FPSCounter.GetAverage() * 1000.0f);
#endif

	return MyString;
}


FString SWidgetReflector::HandlePickButtonText() const
{
	static const FString NotPicking = LOCTEXT("PickWidget", "Pick Widget").ToString();
	static const FString Picking = LOCTEXT("PickingWidget", "Picking (Esc to Stop)").ToString();

	return bIsPicking ? Picking : NotPicking;
}


TSharedRef<ITableRow> SWidgetReflector::HandleReflectorTreeGenerateRow( TSharedPtr<FReflectorNode> InReflectorNode, const TSharedRef<STableViewBase>& OwnerTable )
{
	return SNew(SReflectorTreeWidgetItem, OwnerTable)
		.WidgetInfoToVisualize(InReflectorNode)
		.ToolTip(GenerateToolTipForReflectorNode(InReflectorNode))
		.SourceCodeAccessor(SourceAccessDelegate);
}


TSharedRef<ITableRow> SWidgetReflector::GenerateEventLogRow( TSharedRef<FLoggedEvent> InLoggedEvent, const TSharedRef<STableViewBase>& OwnerTable )
{
	return SNew(STableRow<TSharedRef<FLoggedEvent>>, OwnerTable)
	[
		SNew(STextBlock)
			.Text(InLoggedEvent->ToText())
	];
}


#undef LOCTEXT_NAMESPACE

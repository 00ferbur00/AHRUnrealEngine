// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"

#include "Designer/DesignTimeUtils.h"

#include "Extensions/CanvasSlotExtension.h"
#include "Extensions/GridSlotExtension.h"
#include "Extensions/HorizontalSlotExtension.h"
#include "Extensions/UniformGridSlotExtension.h"
#include "Extensions/VerticalSlotExtension.h"
#include "SDesignerView.h"

#include "BlueprintEditor.h"
#include "SKismetInspector.h"
#include "BlueprintEditorUtils.h"

#include "WidgetTemplateDragDropOp.h"
#include "SZoomPan.h"
#include "SDisappearingBar.h"
#include "SDesignerToolBar.h"
#include "DesignerCommands.h"
#include "STransformHandle.h"
#include "Runtime/Engine/Classes/Engine/RendererSettings.h"
#include "SDPIScaler.h"
#include "SNumericEntryBox.h"

#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintCompiler.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetBlueprintEditorUtils.h"

#include "ObjectEditorUtils.h"
#include "Blueprint/WidgetTree.h"
#include "ScopedTransaction.h"
#include "Settings/WidgetDesignerSettings.h"
#include "Components/CanvasPanelSlot.h"

#define LOCTEXT_NAMESPACE "UMG"

const float HoveredAnimationTime = 0.150f;

class FSelectedWidgetDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FSelectedWidgetDragDropOp, FDecoratedDragDropOp)

	TMap<FName, FString> ExportedSlotProperties;

	FWidgetReference Widget;

	bool bStayingInParent;
	FWidgetReference ParentWidget;

	static TSharedRef<FSelectedWidgetDragDropOp> New(TSharedPtr<FWidgetBlueprintEditor> Editor, FWidgetReference InWidget);
};

TSharedRef<FSelectedWidgetDragDropOp> FSelectedWidgetDragDropOp::New(TSharedPtr<FWidgetBlueprintEditor> Editor, FWidgetReference InWidget)
{
	bool bStayInParent = false;
	if ( UPanelWidget* PanelTemplate = InWidget.GetTemplate()->GetParent() )
	{
		bStayInParent = PanelTemplate->LockToPanelOnDrag();
	}

	TSharedRef<FSelectedWidgetDragDropOp> Operation = MakeShareable(new FSelectedWidgetDragDropOp());
	Operation->Widget = InWidget;
	Operation->bStayingInParent = bStayInParent;
	Operation->ParentWidget = Editor->GetReferenceFromTemplate(InWidget.GetTemplate()->GetParent());
	Operation->DefaultHoverText = FText::FromString( InWidget.GetTemplate()->GetLabel() );
	Operation->CurrentHoverText = FText::FromString( InWidget.GetTemplate()->GetLabel() );
	Operation->Construct();

	FWidgetBlueprintEditorUtils::ExportPropertiesToText(InWidget.GetTemplate()->Slot, Operation->ExportedSlotProperties);

	return Operation;
}

//////////////////////////////////////////////////////////////////////////

static bool LocateWidgetsUnderCursor_Helper(FArrangedWidget& Candidate, FVector2D InAbsoluteCursorLocation, FArrangedChildren& OutWidgetsUnderCursor, bool bIgnoreEnabledStatus)
{
	const bool bCandidateUnderCursor =
		// Candidate is physically under the cursor
		Candidate.Geometry.IsUnderLocation(InAbsoluteCursorLocation);

	bool bHitAnyWidget = false;
	if ( bCandidateUnderCursor )
	{
		// The candidate widget is under the mouse
		OutWidgetsUnderCursor.AddWidget(Candidate);

		// Check to see if we were asked to still allow children to be hit test visible
		bool bHitChildWidget = false;
		
		if ( Candidate.Widget->GetVisibility().AreChildrenHitTestVisible() )//!= 0 || OutWidgetsUnderCursor. )
		{
			FArrangedChildren ArrangedChildren(OutWidgetsUnderCursor.GetFilter());
			Candidate.Widget->ArrangeChildren(Candidate.Geometry, ArrangedChildren);

			// A widget's children are implicitly Z-ordered from first to last
			for ( int32 ChildIndex = ArrangedChildren.Num() - 1; !bHitChildWidget && ChildIndex >= 0; --ChildIndex )
			{
				FArrangedWidget& SomeChild = ArrangedChildren[ChildIndex];
				bHitChildWidget = ( SomeChild.Widget->IsEnabled() || bIgnoreEnabledStatus ) && LocateWidgetsUnderCursor_Helper(SomeChild, InAbsoluteCursorLocation, OutWidgetsUnderCursor, bIgnoreEnabledStatus);
			}
		}

		// If we hit a child widget or we hit our candidate widget then we'll append our widgets
		const bool bHitCandidateWidget = OutWidgetsUnderCursor.Accepts(Candidate.Widget->GetVisibility()) &&
			Candidate.Widget->GetVisibility().AreChildrenHitTestVisible();
		
		bHitAnyWidget = bHitChildWidget || bHitCandidateWidget;
		if ( !bHitAnyWidget )
		{
			// No child widgets were hit, and even though the cursor was over our candidate widget, the candidate
			// widget was not hit-testable, so we won't report it
			check(OutWidgetsUnderCursor.Last() == Candidate);
			OutWidgetsUnderCursor.Remove(OutWidgetsUnderCursor.Num() - 1);
		}
	}

	return bHitAnyWidget;
}

/////////////////////////////////////////////////////
// SDesignerView

const FString SDesignerView::ConfigSectionName = "UMGEditor.Designer";
const uint32 SDesignerView::DefaultResolutionWidth = 1280;
const uint32 SDesignerView::DefaultResolutionHeight = 720;
const FString SDesignerView::DefaultAspectRatio = "16:9";

void SDesignerView::Construct(const FArguments& InArgs, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
{
	ScopedTransaction = NULL;

	PreviewWidget = NULL;
	DropPreviewWidget = NULL;
	DropPreviewParent = NULL;
	BlueprintEditor = InBlueprintEditor;

	DesignerMessage = EDesignerMessage::None;
	TransformMode = ETransformMode::Layout;

	SetStartupResolution();

	ResolutionTextFade = FCurveSequence(0.0f, 1.0f);
	ResolutionTextFade.Play();

	HoverTime = 0;

	bMovingExistingWidget = false;

	// TODO UMG - Register these with the module through some public interface to allow for new extensions to be registered.
	Register(MakeShareable(new FVerticalSlotExtension()));
	Register(MakeShareable(new FHorizontalSlotExtension()));
	Register(MakeShareable(new FCanvasSlotExtension()));
	Register(MakeShareable(new FUniformGridSlotExtension()));
	Register(MakeShareable(new FGridSlotExtension()));

	FWidgetBlueprintCompiler::OnWidgetBlueprintCompiled.AddSP( this, &SDesignerView::OnBlueprintCompiled );

	BindCommands();

	SDesignSurface::Construct(SDesignSurface::FArguments()
		.AllowContinousZoomInterpolation(false)
		.Content()
		[
			SNew(SOverlay)

			// The bottom layer of the overlay where the actual preview widget appears.
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(PreviewHitTestRoot, SZoomPan)
				.Visibility(EVisibility::HitTestInvisible)
				.ZoomAmount(this, &SDesignerView::GetZoomAmount)
				.ViewOffset(this, &SDesignerView::GetViewOffset)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					[
						SNew(SBorder)
						[
							SNew(SSpacer)
							.Size(FVector2D(1, 1))
						]
					]

					+ SOverlay::Slot()
					[
						SNew(SBox)
						.WidthOverride(this, &SDesignerView::GetPreviewWidth)
						.HeightOverride(this, &SDesignerView::GetPreviewHeight)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						.Visibility(EVisibility::SelfHitTestInvisible)
						[
							SAssignNew(PreviewSurface, SDPIScaler)
							.DPIScale(this, &SDesignerView::GetPreviewDPIScale)
							.Visibility(EVisibility::SelfHitTestInvisible)
						]
					]
				]
			]

			// A layer in the overlay where we put all the user intractable widgets, like the reorder widgets.
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(EffectsLayer, SPaintSurface)
				.OnPaintHandler(this, &SDesignerView::HandleEffectsPainting)
			]

			// A layer in the overlay where we put all the user intractable widgets, like the reorder widgets.
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(ExtensionWidgetCanvas, SCanvas)
				.Visibility(EVisibility::SelfHitTestInvisible)
			]

			// Top bar with buttons for changing the designer
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6, 2, 0, 0)
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
					.Text(this, &SDesignerView::GetZoomText)
					.ColorAndOpacity(this, &SDesignerView::GetZoomTextColorAndOpacity)
					.Visibility(EVisibility::SelfHitTestInvisible)
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
					.Size(FVector2D(1, 1))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f)
				[
					SNew(SDesignerToolBar)
					.CommandList(CommandList)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "ViewportMenu.Button")
					.ToolTipText(LOCTEXT("ZoomToFit_ToolTip", "Zoom To Fit"))
					.OnClicked(this, &SDesignerView::HandleZoomToFitClicked)
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("UMGEditor.ZoomToFit"))
					]
				]
				
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f)
				[
					SNew(SComboButton)
					.ButtonStyle(FEditorStyle::Get(), "ViewportMenu.Button")
					.ForegroundColor(FLinearColor::Black)
					.OnGetMenuContent(this, &SDesignerView::GetAspectMenu)
					.ContentPadding(2.0f)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Resolution", "Resolution"))
						.TextStyle(FEditorStyle::Get(), "ViewportMenu.Label")
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<int32>)
					.AllowSpin(true)
					.Delta(5)
					.MinSliderValue(1)
					.MinValue(1)
					.MaxSliderValue(TOptional<int32>(1000))
					.Value(this, &SDesignerView::GetCustomResolutionWidth)
					.OnValueChanged(this, &SDesignerView::OnCustomResolutionWidthChanged)
					.Visibility(this, &SDesignerView::GetCustomResolutionEntryVisibility)
					.MinDesiredValueWidth(50)
					.LabelPadding(0)
					.Label()
					[
						SNumericEntryBox<int32>::BuildLabel(LOCTEXT("Width", "Width"), FLinearColor::White, SNumericEntryBox<int32>::RedLabelBackgroundColor)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<int32>)
					.AllowSpin(true)
					.Delta(5)
					.MinSliderValue(1)
					.MaxSliderValue(TOptional<int32>(1000))
					.MinValue(1)
					.Value(this, &SDesignerView::GetCustomResolutionHeight)
					.OnValueChanged(this, &SDesignerView::OnCustomResolutionHeightChanged)
					.Visibility(this, &SDesignerView::GetCustomResolutionEntryVisibility)
					.MinDesiredValueWidth(50)
					.LabelPadding(0)
					.Label()
					[
						SNumericEntryBox<int32>::BuildLabel(LOCTEXT("Height", "Height"), FLinearColor::White, SNumericEntryBox<int32>::GreenLabelBackgroundColor)
					]
				]
			]

			// Bottom bar to show current resolution & AR
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6, 0, 0, 2)
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
					.Text(this, &SDesignerView::GetCurrentResolutionText)
					.ColorAndOpacity(this, &SDesignerView::GetResolutionTextColorAndOpacity)
				]
			]

			// Info Bar, displays heads up information about some actions.
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SDisappearingBar)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.10, 0.10, 0.10, 0.75))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(0, 5))
					.Visibility(this, &SDesignerView::GetInfoBarVisibility)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
						.Text(this, &SDesignerView::GetInfoBarText)
					]
				]
			]
		]
	);

	BlueprintEditor.Pin()->OnSelectedWidgetsChanged.AddRaw(this, &SDesignerView::OnEditorSelectionChanged);

	ZoomToFit(/*bInstantZoom*/ true);
}

SDesignerView::~SDesignerView()
{
	UWidgetBlueprint* Blueprint = GetBlueprint();
	if ( Blueprint )
	{
		Blueprint->OnChanged().RemoveAll(this);
	}

	if ( BlueprintEditor.IsValid() )
	{
		BlueprintEditor.Pin()->OnSelectedWidgetsChanged.RemoveAll(this);
	}
}

void SDesignerView::BindCommands()
{
	CommandList = MakeShareable(new FUICommandList);

	const FDesignerCommands& Commands = FDesignerCommands::Get();

	CommandList->MapAction(
		Commands.LayoutTransform,
		FExecuteAction::CreateSP(this, &SDesignerView::SetTransformMode, ETransformMode::Layout),
		FCanExecuteAction::CreateSP(this, &SDesignerView::CanSetTransformMode, ETransformMode::Layout),
		FIsActionChecked::CreateSP(this, &SDesignerView::IsTransformModeActive, ETransformMode::Layout)
		);

	CommandList->MapAction(
		Commands.RenderTransform,
		FExecuteAction::CreateSP(this, &SDesignerView::SetTransformMode, ETransformMode::Render),
		FCanExecuteAction::CreateSP(this, &SDesignerView::CanSetTransformMode, ETransformMode::Render),
		FIsActionChecked::CreateSP(this, &SDesignerView::IsTransformModeActive, ETransformMode::Render)
		);
}

void SDesignerView::AddReferencedObjects(FReferenceCollector& Collector)
{
	if ( PreviewWidget )
	{
		Collector.AddReferencedObject(PreviewWidget);
	}
}

void SDesignerView::SetTransformMode(ETransformMode::Type InTransformMode)
{
	if ( !InTransaction() )
	{
		TransformMode = InTransformMode;
	}
}

bool SDesignerView::CanSetTransformMode(ETransformMode::Type InTransformMode) const
{
	return true;
}

bool SDesignerView::IsTransformModeActive(ETransformMode::Type InTransformMode) const
{
	return TransformMode == InTransformMode;
}

void SDesignerView::SetStartupResolution()
{
	// Use previously set resolution (or create new entries using default values)
	// Width
	if (!GConfig->GetInt(*ConfigSectionName, TEXT("PreviewWidth"), PreviewWidth, GEditorUserSettingsIni))
	{
		GConfig->SetInt(*ConfigSectionName, TEXT("PreviewWidth"), DefaultResolutionWidth, GEditorUserSettingsIni);
		PreviewWidth = DefaultResolutionWidth;
	}
	// Height
	if (!GConfig->GetInt(*ConfigSectionName, TEXT("PreviewHeight"), PreviewHeight, GEditorUserSettingsIni))
	{
		GConfig->SetInt(*ConfigSectionName, TEXT("PreviewHeight"), DefaultResolutionHeight, GEditorUserSettingsIni);
		PreviewHeight = DefaultResolutionHeight;
	}
	// Aspect Ratio
	if (!GConfig->GetString(*ConfigSectionName, TEXT("PreviewAspectRatio"), PreviewAspectRatio, GEditorUserSettingsIni))
	{
		GConfig->SetString(*ConfigSectionName, TEXT("PreviewAspectRatio"), *DefaultAspectRatio, GEditorUserSettingsIni);
		PreviewAspectRatio = DefaultAspectRatio;
	}
}

float SDesignerView::GetPreviewScale() const
{
	return GetZoomAmount() * GetPreviewDPIScale();
}

FWidgetReference SDesignerView::GetSelectedWidget() const
{
	return SelectedWidget;
}

ETransformMode::Type SDesignerView::GetTransformMode() const
{
	return TransformMode;
}

FOptionalSize SDesignerView::GetPreviewWidth() const
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		if ( DefaultWidget->bUseDesignTimeSize )
		{
			return DefaultWidget->DesignTimeSize.X;
		}
	}

	return (float)PreviewWidth;
}

FOptionalSize SDesignerView::GetPreviewHeight() const
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		if ( DefaultWidget->bUseDesignTimeSize )
		{
			return DefaultWidget->DesignTimeSize.Y;
		}
	}

	return (float)PreviewHeight;
}

float SDesignerView::GetPreviewDPIScale() const
{
	// If the user is using a custom size then we disable the DPI scaling logic.
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		if ( DefaultWidget->bUseDesignTimeSize )
		{
			return 1.0f;
		}
	}

	return GetDefault<URendererSettings>(URendererSettings::StaticClass())->GetDPIScaleBasedOnSize(FIntPoint(PreviewWidth, PreviewHeight));
}

FSlateRect SDesignerView::ComputeAreaBounds() const
{
	return FSlateRect(0, 0, GetPreviewWidth().Get(), GetPreviewHeight().Get());
}

EVisibility SDesignerView::GetInfoBarVisibility() const
{
	if ( DesignerMessage != EDesignerMessage::None )
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

FText SDesignerView::GetInfoBarText() const
{
	switch ( DesignerMessage )
	{
	case EDesignerMessage::MoveFromParent:
		return LOCTEXT("PressShiftToMove", "Press Alt to move the widget out of the current parent");
	}

	return FText::GetEmpty();
}

void SDesignerView::OnEditorSelectionChanged()
{
	TSet<FWidgetReference> PendingSelectedWidgets = BlueprintEditor.Pin()->GetSelectedWidgets();

	// Notify all widgets that are no longer selected.
	for ( FWidgetReference& WidgetRef : SelectedWidgets )
	{
		if ( WidgetRef.IsValid() && !PendingSelectedWidgets.Contains(WidgetRef) )
		{
			WidgetRef.GetPreview()->Deselect();
		}
	}

	// Notify all widgets that are now selected.
	for ( FWidgetReference& WidgetRef : PendingSelectedWidgets )
	{
		if ( WidgetRef.IsValid() && !SelectedWidgets.Contains(WidgetRef) )
		{
			WidgetRef.GetPreview()->Select();
		}
	}

	SelectedWidgets = PendingSelectedWidgets;

	if ( SelectedWidgets.Num() > 0 )
	{
		for ( FWidgetReference& Widget : SelectedWidgets )
		{
			SelectedWidget = Widget;
			break;
		}
	}
	else
	{
		SelectedWidget = FWidgetReference();
	}

	CreateExtensionWidgetsForSelection();
}

FGeometry SDesignerView::GetDesignerGeometry() const
{
	return CachedDesignerGeometry;
}

void SDesignerView::MarkDesignModifed(bool bRequiresRecompile)
{
	if ( bRequiresRecompile )
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

bool SDesignerView::GetWidgetParentGeometry(const FWidgetReference& Widget, FGeometry& Geometry) const
{
	if ( UWidget* WidgetPreview = Widget.GetPreview() )
	{
		if ( UPanelWidget* Parent = WidgetPreview->GetParent() )
		{
			FWidgetReference ParentReference = BlueprintEditor.Pin()->GetReferenceFromPreview(Parent);
			return GetWidgetGeometry(ParentReference, Geometry);
		}
	}

	Geometry = GetDesignerGeometry();
	return true;
}

bool SDesignerView::GetWidgetGeometry(const FWidgetReference& Widget, FGeometry& Geometry) const
{
	if ( UWidget* WidgetPreview = Widget.GetPreview() )
	{
		TSharedPtr<SWidget> CachedPreviewWidget = WidgetPreview->GetCachedWidget();
		if ( CachedPreviewWidget.IsValid() )
		{
			const FArrangedWidget* ArrangedWidget = CachedWidgetGeometry.Find(CachedPreviewWidget.ToSharedRef());
			if ( ArrangedWidget )
			{
				Geometry = ArrangedWidget->Geometry;
				return true;
			}
		}
	}

	return false;
}

void SDesignerView::ClearExtensionWidgets()
{
	ExtensionWidgetCanvas->ClearChildren();
}

void SDesignerView::CreateExtensionWidgetsForSelection()
{
	// Remove all the current extension widgets
	ClearExtensionWidgets();

	TArray<FWidgetReference> Selected;
	if ( SelectedWidget.IsValid() )
	{
		Selected.Add(SelectedWidget);
	}

	TArray< TSharedRef<FDesignerSurfaceElement> > ExtensionElements;

	if ( Selected.Num() > 0 )
	{
		// Add transform handles
		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::TopLeft), EExtensionLayoutLocation::TopLeft, FVector2D(-10, -10))));
		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::TopCenter), EExtensionLayoutLocation::TopCenter, FVector2D(-5, -10))));
		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::TopRight), EExtensionLayoutLocation::TopRight, FVector2D(0, -10))));

		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::CenterLeft), EExtensionLayoutLocation::CenterLeft, FVector2D(-10, -5))));
		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::CenterRight), EExtensionLayoutLocation::CenterRight, FVector2D(0, -5))));

		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::BottomLeft), EExtensionLayoutLocation::BottomLeft, FVector2D(-10, 0))));
		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::BottomCenter), EExtensionLayoutLocation::BottomCenter, FVector2D(-5, 0))));
		ExtensionElements.Add(MakeShareable(new FDesignerSurfaceElement(SNew(STransformHandle, this, ETransformDirection::BottomRight), EExtensionLayoutLocation::BottomRight, FVector2D(0, 0))));

		// Build extension widgets for new selection
		for ( TSharedRef<FDesignerExtension>& Ext : DesignerExtensions )
		{
			if ( Ext->CanExtendSelection(Selected) )
			{
				Ext->ExtendSelection(Selected, ExtensionElements);
			}
		}

		// Add Widgets to designer surface
		for ( TSharedRef<FDesignerSurfaceElement>& ExtElement : ExtensionElements )
		{
			ExtensionWidgetCanvas->AddSlot()
				.Position(TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP(this, &SDesignerView::GetExtensionPosition, ExtElement)))
				.Size(TAttribute<FVector2D>::Create(TAttribute<FVector2D>::FGetter::CreateSP(this, &SDesignerView::GetExtensionSize, ExtElement)))
				[
					ExtElement->GetWidget()
				];
		}
	}
}

FVector2D SDesignerView::GetExtensionPosition(TSharedRef<FDesignerSurfaceElement> ExtensionElement) const
{
	const FVector2D TopLeft = CachedDesignerWidgetLocation;
	const FVector2D Size = CachedDesignerWidgetSize * GetPreviewScale();

	// Calculate the parent position and size.  We use this information for calculating offsets.
	FVector2D ParentPosition, ParentSize;
	{
		FWidgetReference ParentRef = BlueprintEditor.Pin()->GetReferenceFromTemplate(SelectedWidget.GetTemplate()->GetParent());

		UWidget* Preview = ParentRef.GetPreview();
		TSharedPtr<SWidget> CachedPreviewSlateWidget = Preview ? Preview->GetCachedWidget() : NULL;
		if ( CachedPreviewSlateWidget.IsValid() )
		{
			FWidgetPath WidgetPath;
			SelectedWidgetPath.ToWidgetPath(WidgetPath);
		
			FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
			FDesignTimeUtils::GetArrangedWidgetRelativeToParent(WidgetPath, CachedPreviewSlateWidget.ToSharedRef(), AsShared(), ArrangedWidget);

			ParentPosition = ArrangedWidget.Geometry.AbsolutePosition;
			ParentSize = ArrangedWidget.Geometry.Size * GetPreviewScale();
		}
	}

	FVector2D FinalPosition(0, 0);

	// Get the intial offset based on the location around the selected object.
	switch ( ExtensionElement->GetLocation() )
	{
	case EExtensionLayoutLocation::Absolute:
	{
		FinalPosition = ParentPosition;
		break;
	}
	case EExtensionLayoutLocation::TopLeft:
		FinalPosition = TopLeft;
		break;
	case EExtensionLayoutLocation::TopCenter:
		FinalPosition = TopLeft + FVector2D(Size.X * 0.5f, 0);
		break;
	case EExtensionLayoutLocation::TopRight:
		FinalPosition = TopLeft + FVector2D(Size.X, 0);
		break;

	case EExtensionLayoutLocation::CenterLeft:
		FinalPosition = TopLeft + FVector2D(0, Size.Y * 0.5f);
		break;
	case EExtensionLayoutLocation::CenterCenter:
		FinalPosition = TopLeft + FVector2D(Size.X * 0.5f, Size.Y * 0.5f);
		break;
	case EExtensionLayoutLocation::CenterRight:
		FinalPosition = TopLeft + FVector2D(Size.X, Size.Y * 0.5f);
		break;

	case EExtensionLayoutLocation::BottomLeft:
		FinalPosition = TopLeft + FVector2D(0, Size.Y);
		break;
	case EExtensionLayoutLocation::BottomCenter:
		FinalPosition = TopLeft + FVector2D(Size.X * 0.5f, Size.Y);
		break;
	case EExtensionLayoutLocation::BottomRight:
		FinalPosition = TopLeft + Size;
		break;
	}

	// Add the alignment offset
	FinalPosition += ParentSize * ExtensionElement->GetAlignment();

	return FinalPosition + ExtensionElement->GetOffset();
}

FVector2D SDesignerView::GetExtensionSize(TSharedRef<FDesignerSurfaceElement> ExtensionElement) const
{
	return ExtensionElement->GetWidget()->GetDesiredSize();
}

UWidgetBlueprint* SDesignerView::GetBlueprint() const
{
	if ( BlueprintEditor.IsValid() )
	{
		UBlueprint* BP = BlueprintEditor.Pin()->GetBlueprintObj();
		return Cast<UWidgetBlueprint>(BP);
	}

	return NULL;
}

void SDesignerView::Register(TSharedRef<FDesignerExtension> Extension)
{
	Extension->Initialize(this, GetBlueprint());
	DesignerExtensions.Add(Extension);
}

void SDesignerView::OnBlueprintCompiled(UBlueprint* InBlueprint)
{
	// Because widget blueprints can contain other widget blueprints, the safe thing to do is to have all
	// designers jettison their previews on the compilation of any widget blueprint.  We do this to prevent
	// having slate widgets that still may reference data in their owner UWidget that has been garbage collected.
	CachedWidgetGeometry.Reset();

	PreviewWidget = NULL;
	PreviewSurface->SetContent(SNullWidget::NullWidget);
}

FWidgetReference SDesignerView::GetWidgetAtCursor(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FArrangedWidget& ArrangedWidget)
{
	//@TODO UMG Make it so you can request dropable widgets only, to find the first parentable.

	FArrangedChildren Children(EVisibility::All);

	PreviewHitTestRoot->SetVisibility(EVisibility::Visible);
	FArrangedWidget WindowWidgetGeometry(PreviewHitTestRoot.ToSharedRef(), MyGeometry);
	LocateWidgetsUnderCursor_Helper(WindowWidgetGeometry, MouseEvent.GetScreenSpacePosition(), Children, true);

	PreviewHitTestRoot->SetVisibility(EVisibility::HitTestInvisible);

	UUserWidget* WidgetActor = BlueprintEditor.Pin()->GetPreview();
	if ( WidgetActor )
	{
		UWidget* Preview = NULL;

		for ( int32 ChildIndex = Children.Num() - 1; ChildIndex >= 0; ChildIndex-- )
		{
			FArrangedWidget& Child = Children.GetInternalArray()[ChildIndex];
			Preview = WidgetActor->GetWidgetHandle(Child.Widget);
			
			// Ignore the drop preview widget when doing widget picking
			if (Preview == DropPreviewWidget)
			{
				Preview = NULL;
				continue;
			}
			
			if ( Preview )
			{
				ArrangedWidget = Child;
				break;
			}
		}

		if ( Preview )
		{
			return BlueprintEditor.Pin()->GetReferenceFromPreview(Preview);
		}
	}

	return FWidgetReference();
}

void SDesignerView::ResolvePendingSelectedWidgets()
{
	if ( PendingSelectedWidget.IsValid() )
	{
		TSet<FWidgetReference> SelectedTemplates;
		SelectedTemplates.Add(PendingSelectedWidget);
		BlueprintEditor.Pin()->SelectWidgets(SelectedTemplates);

		PendingSelectedWidget = FWidgetReference();
	}
}

FReply SDesignerView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SDesignSurface::OnMouseButtonDown(MyGeometry, MouseEvent);

	//TODO UMG Undoable Selection
	FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
	FWidgetReference NewSelectedWidget = GetWidgetAtCursor(MyGeometry, MouseEvent, ArrangedWidget);
	SelectedWidgetContextMenuLocation = ArrangedWidget.Geometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if ( NewSelectedWidget.IsValid() )
	{
		PendingSelectedWidget = NewSelectedWidget;

		if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
		{
			const bool bResolvePendingSelectionImmediately =
				!SelectedWidget.IsValid() ||
				!NewSelectedWidget.GetTemplate()->IsChildOf(SelectedWidget.GetTemplate()) ||
				SelectedWidget.GetTemplate()->GetParent() == nullptr;

			// If the newly clicked item is a child of the active selection, add it to the pending set of selected 
			// widgets, if they begin dragging we can just move the parent, but if it's not part of the parent set, 
			// we want to immediately begin dragging it.  Also if the currently selected widget is the root widget, 
			// we won't be moving him so just resolve immediately.
			if ( bResolvePendingSelectionImmediately )
			{
				ResolvePendingSelectedWidgets();
			}

			DraggingStartPositionScreenSpace = MouseEvent.GetScreenSpacePosition();
		}
	}

	// Capture mouse for the drag handle and general mouse actions
	return FReply::Handled().PreventThrottling().SetKeyboardFocus(AsShared(), EKeyboardFocusCause::Mouse).CaptureMouse(AsShared());
}

FReply SDesignerView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( HasMouseCapture() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
	{
		ResolvePendingSelectedWidgets();

		bMovingExistingWidget = false;
		DesignerMessage = EDesignerMessage::None;

		EndTransaction(false);
	}
	else if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		if ( !bIsPanning )
		{
			ResolvePendingSelectedWidgets();

			ShowContextMenu(MyGeometry, MouseEvent);
		}
	}

	SDesignSurface::OnMouseButtonUp(MyGeometry, MouseEvent);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SDesignerView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( MouseEvent.GetCursorDelta().IsZero() )
	{
		return FReply::Unhandled();
	}

	FReply SurfaceHandled = SDesignSurface::OnMouseMove(MyGeometry, MouseEvent);
	if ( SurfaceHandled.IsEventHandled() )
	{
		return SurfaceHandled;
	}

	if ( MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) )
	{
		if ( SelectedWidget.IsValid() && !bMovingExistingWidget )
		{
			if ( TransformMode == ETransformMode::Layout )
			{
				const bool bIsRootWidget = SelectedWidget.GetTemplate()->GetParent() == NULL;
				if ( !bIsRootWidget )
				{
					bMovingExistingWidget = true;
					//Drag selected widgets
					return FReply::Handled().DetectDrag(AsShared(), EKeys::LeftMouseButton);
				}
			}
			else
			{
				checkSlow(TransformMode == ETransformMode::Render);
				checkSlow(bMovingExistingWidget == false);

				BeginTransaction(LOCTEXT("MoveWidgetRT", "Move Widget (Render Transform)"));

				if ( UWidget* WidgetPreview = SelectedWidget.GetPreview() )
				{
					FGeometry ParentGeometry;
					if ( GetWidgetParentGeometry(SelectedWidget, ParentGeometry) )
					{
						const FSlateRenderTransform& AbsoluteToLocalTransform = Inverse(ParentGeometry.GetAccumulatedRenderTransform());

						FWidgetTransform RenderTransform = WidgetPreview->RenderTransform;
						RenderTransform.Translation += AbsoluteToLocalTransform.TransformVector(MouseEvent.GetCursorDelta());

						static const FName RenderTransformName(TEXT("RenderTransform"));

						FObjectEditorUtils::SetPropertyValue<UWidget, FWidgetTransform>(WidgetPreview, RenderTransformName, RenderTransform);
						FObjectEditorUtils::SetPropertyValue<UWidget, FWidgetTransform>(SelectedWidget.GetTemplate(), RenderTransformName, RenderTransform);
					}
				}
			}
		}
	}
	
	// Update the hovered widget under the mouse
	FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
	FWidgetReference NewHoveredWidget = GetWidgetAtCursor(MyGeometry, MouseEvent, ArrangedWidget);
	if ( !( NewHoveredWidget == HoveredWidget ) )
	{
		HoveredWidget = NewHoveredWidget;
		HoverTime = 0;
	}

	return FReply::Unhandled();
}

void SDesignerView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	HoveredWidget = FWidgetReference();
	HoverTime = 0;
}

void SDesignerView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	HoveredWidget = FWidgetReference();
	HoverTime = 0;
}

FReply SDesignerView::OnKeyDown(const FGeometry& MyGeometry, const FKeyboardEvent& InKeyboardEvent)
{
	BlueprintEditor.Pin()->PasteDropLocation = FVector2D(0, 0);

	if ( BlueprintEditor.Pin()->DesignerCommandList->ProcessCommandBindings(InKeyboardEvent) )
	{
		return FReply::Handled();
	}

	if ( CommandList->ProcessCommandBindings(InKeyboardEvent) )
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SDesignerView::ShowContextMenu(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FMenuBuilder MenuBuilder(true, NULL);

	FWidgetBlueprintEditorUtils::CreateWidgetContextMenu(MenuBuilder, BlueprintEditor.Pin().ToSharedRef(), SelectedWidgetContextMenuLocation);

	TSharedPtr<SWidget> MenuContent = MenuBuilder.MakeWidget();

	if ( MenuContent.IsValid() )
	{
		FVector2D SummonLocation = MouseEvent.GetScreenSpacePosition();
		FSlateApplication::Get().PushMenu(AsShared(), MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}
}

void SDesignerView::PopulateWidgetGeometryCache(FArrangedWidget& Root)
{
	FArrangedChildren ArrangedChildren(EVisibility::All);
	Root.Widget->ArrangeChildren(Root.Geometry, ArrangedChildren);

	CachedWidgetGeometry.Add(Root.Widget, Root);

	// A widget's children are implicitly Z-ordered from first to last
	for ( int32 ChildIndex = ArrangedChildren.Num() - 1; ChildIndex >= 0; --ChildIndex )
	{
		FArrangedWidget& SomeChild = ArrangedChildren[ChildIndex];
		PopulateWidgetGeometryCache(SomeChild);
	}
}

void SDesignerView::CacheSelectedWidgetGeometry()
{
	if ( SelectedSlateWidget.IsValid() )
	{
		TSharedRef<SWidget> Widget = SelectedSlateWidget.Pin().ToSharedRef();

		FWidgetPath WidgetPath;
		if ( FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget, WidgetPath) )
		{
			SelectedWidgetPath = FWeakWidgetPath(WidgetPath);
		}
		else
		{
			SelectedWidgetPath = FWeakWidgetPath();
		}

		FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
		FDesignTimeUtils::GetArrangedWidgetRelativeToParent(WidgetPath, Widget, AsShared(), ArrangedWidget);

		CachedDesignerWidgetLocation = ArrangedWidget.Geometry.AbsolutePosition;
		CachedDesignerWidgetSize = ArrangedWidget.Geometry.Size;
	}
}

int32 SDesignerView::HandleEffectsPainting(const FOnPaintHandlerParams& PaintArgs)
{
	TSet<FWidgetReference> Selected;
	Selected.Add(SelectedWidget);

	// Allow the extensions to paint anything they want.
	for ( const TSharedRef<FDesignerExtension>& Ext : DesignerExtensions )
	{
		Ext->Paint(Selected, PaintArgs.Geometry, PaintArgs.ClippingRect, PaintArgs.OutDrawElements, PaintArgs.Layer);
	}

	static const FName SelectionOutlineName("UMGEditor.SelectionOutline");
	const FSlateBrush* SelectionOutlineBrush = FEditorStyle::Get().GetBrush(SelectionOutlineName);
	FVector2D SelectionBrushInflationAmount = FVector2D(16, 16) * FVector2D(SelectionOutlineBrush->Margin.Left, SelectionOutlineBrush->Margin.Top) * ( 1.0f / GetPreviewScale() );

	// Don't draw the hovered effect if it's also the selected widget
	if ( HoveredSlateWidget.IsValid() && HoveredSlateWidget != SelectedSlateWidget )
	{
		TSharedRef<SWidget> Widget = HoveredSlateWidget.Pin().ToSharedRef();

		FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
		FDesignTimeUtils::GetArrangedWidgetRelativeToWindow(Widget, ArrangedWidget);

		// Draw hovered effect
		// Azure = 0x007FFF
		const FLinearColor HoveredTint(0, 0.5, 1, FMath::Clamp(HoverTime / HoveredAnimationTime, 0.0f, 1.0f));

		FPaintGeometry HoveredGeometry = ArrangedWidget.Geometry.ToInflatedPaintGeometry(SelectionBrushInflationAmount);

		FSlateDrawElement::MakeBox(
			PaintArgs.OutDrawElements,
			PaintArgs.Layer,
			HoveredGeometry,
			SelectionOutlineBrush,
			PaintArgs.ClippingRect,
			ESlateDrawEffect::None,
			HoveredTint
			);
	}

	if ( SelectedSlateWidget.IsValid() )
	{
		TSharedRef<SWidget> Widget = SelectedSlateWidget.Pin().ToSharedRef();

		FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
		FDesignTimeUtils::GetArrangedWidgetRelativeToWindow(Widget, ArrangedWidget);

		const FLinearColor Tint(0, 1, 0);

		// Draw selection effect

		FPaintGeometry SelectionGeometry = ArrangedWidget.Geometry.ToInflatedPaintGeometry(SelectionBrushInflationAmount);

		FSlateDrawElement::MakeBox(
			PaintArgs.OutDrawElements,
			PaintArgs.Layer,
			SelectionGeometry,
			SelectionOutlineBrush,
			PaintArgs.ClippingRect,
			ESlateDrawEffect::None,
			Tint
			);
	}

	return PaintArgs.Layer + 1;
}

void SDesignerView::UpdatePreviewWidget(bool bForceUpdate)
{
	UUserWidget* LatestPreviewWidget = BlueprintEditor.Pin()->GetPreview();

	if ( LatestPreviewWidget != PreviewWidget || bForceUpdate )
	{
		PreviewWidget = LatestPreviewWidget;
		if ( PreviewWidget )
		{
			TSharedRef<SWidget> NewPreviewSlateWidget = PreviewWidget->TakeWidget();
			NewPreviewSlateWidget->SlatePrepass();

			PreviewSlateWidget = NewPreviewSlateWidget;
			PreviewSurface->SetContent(NewPreviewSlateWidget);

			// Notify all selected widgets that they are selected, because there are new preview objects
			// state may have been lost so this will recreate it if the widget does something special when
			// selected.
			for ( FWidgetReference& WidgetRef : SelectedWidgets )
			{
				if ( WidgetRef.IsValid() )
				{
					WidgetRef.GetPreview()->Select();
				}
			}
		}
		else
		{
			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoWidgetPreview", "No Widget Preview"))
				]
			];
		}
	}
}

void SDesignerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CachedDesignerGeometry = AllottedGeometry;
	HoverTime += InDeltaTime;

	const bool bForceUpdate = false;
	UpdatePreviewWidget(bForceUpdate);

	// Update the selected widget to match the selected template.
	if ( PreviewWidget )
	{
		if ( SelectedWidget.IsValid() )
		{
			// Set the selected widget so that we can draw the highlight
			SelectedSlateWidget = PreviewWidget->GetWidgetFromName(SelectedWidget.GetTemplate()->GetFName());
		}
		else
		{
			SelectedSlateWidget.Reset();
		}

		if ( HoveredWidget.IsValid() )
		{
			HoveredSlateWidget = PreviewWidget->GetWidgetFromName(HoveredWidget.GetTemplate()->GetFName());
		}
		else
		{
			HoveredSlateWidget.Reset();
		}
	}

	// Perform an arrange children pass to cache the geometry of all widgets so that we can query it later.
	CachedWidgetGeometry.Reset();
	FArrangedWidget WindowWidgetGeometry(PreviewHitTestRoot.ToSharedRef(), AllottedGeometry);
	PopulateWidgetGeometryCache(WindowWidgetGeometry);

	CacheSelectedWidgetGeometry();

	// Tick all designer extensions in case they need to update widgets
	for ( const TSharedRef<FDesignerExtension>& Ext : DesignerExtensions )
	{
		Ext->Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	}

	SDesignSurface::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

FReply SDesignerView::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( SelectedWidget.IsValid() )
	{
		// Clear any pending selected widgets, the user has already decided what widget they want.
		PendingSelectedWidget = FWidgetReference();

		// Determine The offset to keep the widget from the mouse while dragging
		FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
		FDesignTimeUtils::GetArrangedWidget(SelectedWidget.GetPreview()->GetCachedWidget().ToSharedRef(), ArrangedWidget);
		SelectedWidgetContextMenuLocation = ArrangedWidget.Geometry.AbsoluteToLocal(DraggingStartPositionScreenSpace);

		ClearExtensionWidgets();

		return FReply::Handled().BeginDragDrop(FSelectedWidgetDragDropOp::New(BlueprintEditor.Pin(), SelectedWidget));
	}

	return FReply::Unhandled();
}

void SDesignerView::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//@TODO UMG Drop Feedback
}

void SDesignerView::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDecoratedDragDropOp> DecoratedDragDropOp = DragDropEvent.GetOperationAs<FDecoratedDragDropOp>();
	if ( DecoratedDragDropOp.IsValid() )
	{
		DecoratedDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());
		DecoratedDragDropOp->ResetToDefaultToolTip();
	}
	
	if (DropPreviewWidget)
	{
		if ( DropPreviewParent )
		{
			DropPreviewParent->RemoveChild(DropPreviewWidget);
		}

		UWidgetBlueprint* BP = GetBlueprint();
		BP->WidgetTree->RemoveWidget(DropPreviewWidget);
		DropPreviewWidget = NULL;
	}
}

FReply SDesignerView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	UWidgetBlueprint* BP = GetBlueprint();
	
	if (DropPreviewWidget)
	{
		if (DropPreviewParent)
		{
			DropPreviewParent->RemoveChild(DropPreviewWidget);
		}
		
		BP->WidgetTree->RemoveWidget(DropPreviewWidget);
		DropPreviewWidget = NULL;
	}
	
	const bool bIsPreview = true;
	DropPreviewWidget = ProcessDropAndAddWidget(MyGeometry, DragDropEvent, bIsPreview);
	if ( DropPreviewWidget )
	{
		//@TODO UMG Drop Feedback
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

UWidget* SDesignerView::ProcessDropAndAddWidget(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, bool bIsPreview)
{
	// In order to prevent the GetWidgetAtCursor code from picking the widget we're about to move, we need to mark it
	// as the drop preview widget before any other code can run.
	TSharedPtr<FSelectedWidgetDragDropOp> SelectedDragDropOp = DragDropEvent.GetOperationAs<FSelectedWidgetDragDropOp>();
	if ( SelectedDragDropOp.IsValid() )
	{
		DropPreviewWidget = SelectedDragDropOp->Widget.GetPreview();
	}

	UWidgetBlueprint* BP = GetBlueprint();

	if ( DropPreviewWidget )
	{
		if ( DropPreviewParent )
		{
			DropPreviewParent->RemoveChild(DropPreviewWidget);
		}

		BP->WidgetTree->RemoveWidget(DropPreviewWidget);
		DropPreviewWidget = NULL;
	}

	FArrangedWidget ArrangedWidget(SNullWidget::NullWidget, FGeometry());
	FWidgetReference WidgetUnderCursor = GetWidgetAtCursor(MyGeometry, DragDropEvent, ArrangedWidget);

	FGeometry WidgetUnderCursorGeometry = ArrangedWidget.Geometry;

	UWidget* Target = NULL;
	if ( WidgetUnderCursor.IsValid() )
	{
		Target = bIsPreview ? WidgetUnderCursor.GetPreview() : WidgetUnderCursor.GetTemplate();
	}

	TSharedPtr<FWidgetTemplateDragDropOp> TemplateDragDropOp = DragDropEvent.GetOperationAs<FWidgetTemplateDragDropOp>();
	if ( TemplateDragDropOp.IsValid() )
	{
		TemplateDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());

		// If there's no root widget go ahead and add the widget into the root slot.
		if ( BP->WidgetTree->RootWidget == NULL )
		{
			FScopedTransaction Transaction(LOCTEXT("Designer_AddWidget", "Add Widget"));

			if ( !bIsPreview )
			{
				BP->WidgetTree->SetFlags(RF_Transactional);
				BP->WidgetTree->Modify();
			}

			// TODO UMG This method isn't great, maybe the user widget should just be a canvas.

			// Add it to the root if there are no other widgets to add it to.
			UWidget* Widget = TemplateDragDropOp->Template->Create(BP->WidgetTree);
			Widget->SetIsDesignTime(true);

			BP->WidgetTree->RootWidget = Widget;

			SelectedWidget = BlueprintEditor.Pin()->GetReferenceFromTemplate(Widget);

			DropPreviewParent = NULL;

			if ( bIsPreview )
			{
				Transaction.Cancel();
			}

			return Widget;
		}
		// If there's already a root widget we need to try and place our widget into a parent widget that we've picked against
		else if ( Target && Target->IsA(UPanelWidget::StaticClass()) )
		{
			UPanelWidget* Parent = Cast<UPanelWidget>(Target);

			FScopedTransaction Transaction(LOCTEXT("Designer_AddWidget", "Add Widget"));

			// If this isn't a preview operation we need to modify a few things to properly undo the operation.
			if ( !bIsPreview )
			{
				Parent->SetFlags(RF_Transactional);
				Parent->Modify();

				BP->WidgetTree->SetFlags(RF_Transactional);
				BP->WidgetTree->Modify();
			}

			// Construct the widget and mark it for design time rendering.
			UWidget* Widget = TemplateDragDropOp->Template->Create(BP->WidgetTree);
			Widget->SetIsDesignTime(true);

			// Determine local position inside the parent widget and add the widget to the slot.
			FVector2D LocalPosition = WidgetUnderCursorGeometry.AbsoluteToLocal(DragDropEvent.GetScreenSpacePosition());
			if ( UPanelSlot* Slot = Parent->AddChild(Widget) )
			{
				// HACK UMG - This seems like a bad idea to call TakeWidget
				TSharedPtr<SWidget> SlateWidget = Widget->TakeWidget();
				SlateWidget->SlatePrepass();
				const FVector2D& WidgetDesiredSize = SlateWidget->GetDesiredSize();

				static const FVector2D MinimumDefaultSize(100, 40);
				FVector2D LocalSize = FVector2D(FMath::Max(WidgetDesiredSize.X, MinimumDefaultSize.X), FMath::Max(WidgetDesiredSize.Y, MinimumDefaultSize.Y));

				const UWidgetDesignerSettings* DesignerSettings = GetDefault<UWidgetDesignerSettings>();
				if ( DesignerSettings->GridSnapEnabled )
				{
					LocalPosition.X = ( (int32)LocalPosition.X ) - (( (int32)LocalPosition.X ) % DesignerSettings->GridSnapSize);
					LocalPosition.Y = ( (int32)LocalPosition.Y ) - (( (int32)LocalPosition.Y ) % DesignerSettings->GridSnapSize);
				}

				Slot->SetDesiredPosition(LocalPosition);
				Slot->SetDesiredSize(LocalSize);

				DropPreviewParent = Parent;

				if ( bIsPreview )
				{
					Transaction.Cancel();
				}

				return Widget;
			}
			else
			{
				TemplateDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);

				// TODO UMG ERROR Slot can not be created because maybe the max children has been reached.
				//          Maybe we can traverse the hierarchy and add it to the first parent that will accept it?
			}

			if ( bIsPreview )
			{
				Transaction.Cancel();
			}
		}
		else
		{
			TemplateDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
		}
	}

	// Attempt to deal with moving widgets from a drag operation.
	if ( SelectedDragDropOp.IsValid() )
	{
		SelectedDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());

		// If they've pressed alt, and we were staying in the parent, disable that
		// and adjust the designer message to no longer warn.
		if ( DragDropEvent.IsAltDown() && SelectedDragDropOp->bStayingInParent )
		{
			SelectedDragDropOp->bStayingInParent = false;
			DesignerMessage = EDesignerMessage::None;
		}

		// If we're staying in the parent we started in, replace the parent found under the cursor with
		// the original one, also update the arranged widget data so that our layout calculations are accurate.
		if ( SelectedDragDropOp->bStayingInParent )
		{
			DesignerMessage = EDesignerMessage::MoveFromParent;

			WidgetUnderCursorGeometry = GetDesignerGeometry();
			if ( GetWidgetGeometry(SelectedDragDropOp->ParentWidget, WidgetUnderCursorGeometry) )
			{
				Target = bIsPreview ? SelectedDragDropOp->ParentWidget.GetPreview() : SelectedDragDropOp->ParentWidget.GetTemplate();
			}
		}

		// If the widget being hovered over is a panel, attempt to place it into that panel.
		if ( Target && Target->IsA(UPanelWidget::StaticClass()) )
		{
			UPanelWidget* NewParent = Cast<UPanelWidget>(Target);

			FScopedTransaction Transaction(LOCTEXT("Designer_MoveWidget", "Move Widget"));

			// If this isn't a preview operation we need to modify a few things to properly undo the operation.
			if ( !bIsPreview )
			{
				NewParent->SetFlags(RF_Transactional);
				NewParent->Modify();

				BP->WidgetTree->SetFlags(RF_Transactional);
				BP->WidgetTree->Modify();
			}

			UWidget* Widget = bIsPreview ? SelectedDragDropOp->Widget.GetPreview() : SelectedDragDropOp->Widget.GetTemplate();
			check(Widget);

			if ( Widget )
			{
				if ( Widget->GetParent() )
				{
					if ( !bIsPreview )
					{
						Widget->GetParent()->Modify();
					}

					Widget->GetParent()->RemoveChild(Widget);
				}

				FVector2D ScreenSpacePosition = DragDropEvent.GetScreenSpacePosition();

				const UWidgetDesignerSettings* DesignerSettings = GetDefault<UWidgetDesignerSettings>();
				bool bGridSnapX, bGridSnapY;
				bGridSnapX = bGridSnapY = DesignerSettings->GridSnapEnabled;

				// As long as shift is pressed and we're staying in the same parent,
				// allow the user to lock the movement to a specific axis.
				const bool bLockToAxis =
					FSlateApplication::Get().GetModifierKeys().IsShiftDown() &&
					SelectedDragDropOp->bStayingInParent;

				if ( bLockToAxis )
				{
					// Choose the largest axis of movement as the primary axis to lock to.
					FVector2D DragDelta = ScreenSpacePosition - DraggingStartPositionScreenSpace;
					if ( FMath::Abs(DragDelta.X) > FMath::Abs(DragDelta.Y) )
					{
						// Lock to X Axis
						ScreenSpacePosition.Y = DraggingStartPositionScreenSpace.Y;
						bGridSnapY = false;
					}
					else
					{
						// Lock To Y Axis
						ScreenSpacePosition.X = DraggingStartPositionScreenSpace.X;
						bGridSnapX = false;
					}
				}

				FVector2D LocalPosition = WidgetUnderCursorGeometry.AbsoluteToLocal(ScreenSpacePosition);
				if ( UPanelSlot* Slot = NewParent->AddChild(Widget) )
				{
					FVector2D NewPosition = LocalPosition - SelectedWidgetContextMenuLocation;

					// Perform grid snapping on X and Y if we need to.
					if ( bGridSnapX )
					{
						NewPosition.X = ( (int32)NewPosition.X ) - ( ( (int32)NewPosition.X ) % DesignerSettings->GridSnapSize );
					}

					if ( bGridSnapY )
					{
						NewPosition.Y = ( (int32)NewPosition.Y ) - ( ( (int32)NewPosition.Y ) % DesignerSettings->GridSnapSize );
					}

					// HACK UMG: In order to correctly drop items into the canvas that have a non-zero anchor,
					// we need to know the layout information after slate has performed a prepass.  So we have
					// to rebase the layout and reinterpret the new position based on anchor point layout data.
					// This should be pulled out into an extension of some kind so that this can be fixed for
					// other widgets as well that may need to do work like this.
					if ( UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot) )
					{
						if ( bIsPreview )
						{
							FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Slot, SelectedDragDropOp->ExportedSlotProperties);

							CanvasSlot->SaveBaseLayout();
							Slot->SetDesiredPosition(NewPosition);
							CanvasSlot->RebaseLayout();

							FWidgetBlueprintEditorUtils::ExportPropertiesToText(Slot, SelectedDragDropOp->ExportedSlotProperties);
						}
						else
						{
							FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Slot, SelectedDragDropOp->ExportedSlotProperties);
						}
					}
					else
					{
						FWidgetBlueprintEditorUtils::ImportPropertiesFromText(Slot, SelectedDragDropOp->ExportedSlotProperties);
						Slot->SetDesiredPosition(NewPosition);
					}

					DropPreviewParent = NewParent;

					if ( bIsPreview )
					{
						Transaction.Cancel();
					}

					return Widget;
				}
				else
				{
					SelectedDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);

					// TODO UMG ERROR Slot can not be created because maybe the max children has been reached.
					//          Maybe we can traverse the hierarchy and add it to the first parent that will accept it?
				}

				if ( bIsPreview )
				{
					Transaction.Cancel();
				}
			}
		}
		else
		{
			SelectedDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
		}
	}
	
	return NULL;
}

FReply SDesignerView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bMovingExistingWidget = false;

	UWidgetBlueprint* BP = GetBlueprint();
	
	if (DropPreviewWidget)
	{
		if (DropPreviewParent)
		{
			DropPreviewParent->RemoveChild(DropPreviewWidget);
		}
		
		BP->WidgetTree->RemoveWidget(DropPreviewWidget);
		DropPreviewWidget = NULL;
	}
	
	const bool bIsPreview = false;
	UWidget* Widget = ProcessDropAndAddWidget(MyGeometry, DragDropEvent, bIsPreview);
	if ( Widget )
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

		// Regenerate extension widgets now that we've finished moving or placing the widget.
		CreateExtensionWidgetsForSelection();

		DesignerMessage = EDesignerMessage::None;

		return FReply::Handled();
	}

	DesignerMessage = EDesignerMessage::None;
	
	return FReply::Unhandled();
}

FText SDesignerView::GetResolutionText(int32 Width, int32 Height, const FString& AspectRatio) const
{
	FInternationalization& I18N = FInternationalization::Get();
	FFormatNamedArguments Args;
	Args.Add(TEXT("Width"), FText::AsNumber(Width, nullptr, I18N.GetInvariantCulture()));
	Args.Add(TEXT("Height"), FText::AsNumber(Height, nullptr, I18N.GetInvariantCulture()));
	Args.Add(TEXT("AspectRatio"), FText::FromString(AspectRatio));

	return FText::Format(LOCTEXT("CommonResolutionFormat", "{Width} x {Height} ({AspectRatio})"), Args);
}

FText SDesignerView::GetCurrentResolutionText() const
{
	return GetResolutionText(PreviewWidth, PreviewHeight, PreviewAspectRatio);
}

FSlateColor SDesignerView::GetResolutionTextColorAndOpacity() const
{
	return FLinearColor(1, 1, 1, 1.25f - ResolutionTextFade.GetLerp());
}

void SDesignerView::HandleOnCommonResolutionSelected(int32 Width, int32 Height, FString AspectRatio)
{
	PreviewWidth = Width;
	PreviewHeight = Height;
	PreviewAspectRatio = AspectRatio;

	GConfig->SetInt(*ConfigSectionName, TEXT("PreviewWidth"), Width, GEditorUserSettingsIni);
	GConfig->SetInt(*ConfigSectionName, TEXT("PreviewHeight"), Height, GEditorUserSettingsIni);
	GConfig->SetString(*ConfigSectionName, TEXT("PreviewAspectRatio"), *AspectRatio, GEditorUserSettingsIni);

	// We're no longer using a custom design time size.
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		DefaultWidget->bUseDesignTimeSize = false;
		MarkDesignModifed(/*bRequiresRecompile*/ false);
	}

	ResolutionTextFade.Play();
}

bool SDesignerView::HandleIsCommonResolutionSelected(int32 Width, int32 Height) const
{
	// If we're using a custom design time size, none of the other resolutions should appear selected, even if they match.
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		if ( DefaultWidget->bUseDesignTimeSize )
		{
			return false;
		}
	}
	
	return ( Width == PreviewWidth ) && ( Height == PreviewHeight );
}

void SDesignerView::AddScreenResolutionSection(FMenuBuilder& MenuBuilder, const TArray<FPlayScreenResolution>& Resolutions, const FText& SectionName)
{
	MenuBuilder.BeginSection(NAME_None, SectionName);
	{
		for ( auto Iter = Resolutions.CreateConstIterator(); Iter; ++Iter )
		{
			// Actions for the resolution menu entry
			FExecuteAction OnResolutionSelected = FExecuteAction::CreateRaw(this, &SDesignerView::HandleOnCommonResolutionSelected, Iter->Width, Iter->Height, Iter->AspectRatio);
			FIsActionChecked OnIsResolutionSelected = FIsActionChecked::CreateRaw(this, &SDesignerView::HandleIsCommonResolutionSelected, Iter->Width, Iter->Height);
			FUIAction Action(OnResolutionSelected, FCanExecuteAction(), OnIsResolutionSelected);

			MenuBuilder.AddMenuEntry(FText::FromString(Iter->Description), GetResolutionText(Iter->Width, Iter->Height, Iter->AspectRatio), FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::Check);
		}
	}
	MenuBuilder.EndSection();
}

bool SDesignerView::HandleIsCustomResolutionSelected() const
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		return DefaultWidget->bUseDesignTimeSize;
	}

	return false;
}

void SDesignerView::HandleOnCustomResolutionSelected()
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		DefaultWidget->bUseDesignTimeSize = true;
		MarkDesignModifed(/*bRequiresRecompile*/ false);
	}
}

TOptional<int32> SDesignerView::GetCustomResolutionWidth() const
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		return DefaultWidget->DesignTimeSize.X;
	}

	return 1;
}

TOptional<int32> SDesignerView::GetCustomResolutionHeight() const
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		return DefaultWidget->DesignTimeSize.Y;
	}

	return 1;
}

void SDesignerView::OnCustomResolutionWidthChanged(int32 InValue)
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		DefaultWidget->DesignTimeSize.X = InValue;
		MarkDesignModifed(/*bRequiresRecompile*/ false);
	}
}

void SDesignerView::OnCustomResolutionHeightChanged(int32 InValue)
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		DefaultWidget->DesignTimeSize.Y = InValue;
		MarkDesignModifed(/*bRequiresRecompile*/ false);
	}
}

EVisibility SDesignerView::GetCustomResolutionEntryVisibility() const
{
	if ( UUserWidget* DefaultWidget = GetDefaultWidget() )
	{
		return DefaultWidget->bUseDesignTimeSize ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return EVisibility::Collapsed;
}

UUserWidget* SDesignerView::GetDefaultWidget() const
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	if ( UUserWidget* Default = BPEd->GetWidgetBlueprintObj()->GeneratedClass->GetDefaultObject<UUserWidget>() )
	{
		return Default;
	}

	return nullptr;
}

TSharedRef<SWidget> SDesignerView::GetAspectMenu()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();
	FMenuBuilder MenuBuilder(true, NULL);

	// Add custom option
	FExecuteAction OnResolutionSelected = FExecuteAction::CreateRaw(this, &SDesignerView::HandleOnCustomResolutionSelected);
	FIsActionChecked OnIsResolutionSelected = FIsActionChecked::CreateRaw(this, &SDesignerView::HandleIsCustomResolutionSelected);
	FUIAction Action(OnResolutionSelected, FCanExecuteAction(), OnIsResolutionSelected);

	MenuBuilder.AddMenuEntry(LOCTEXT("Custom", "Custom"), LOCTEXT("Custom", "Custom"), FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::Check);

	// Add the normal set of resultion options.
	AddScreenResolutionSection(MenuBuilder, PlaySettings->PhoneScreenResolutions, LOCTEXT("CommonPhonesSectionHeader", "Phones"));
	AddScreenResolutionSection(MenuBuilder, PlaySettings->TabletScreenResolutions, LOCTEXT("CommonTabletsSectionHeader", "Tablets"));
	AddScreenResolutionSection(MenuBuilder, PlaySettings->LaptopScreenResolutions, LOCTEXT("CommonLaptopsSectionHeader", "Laptops"));
	AddScreenResolutionSection(MenuBuilder, PlaySettings->MonitorScreenResolutions, LOCTEXT("CommoMonitorsSectionHeader", "Monitors"));
	AddScreenResolutionSection(MenuBuilder, PlaySettings->TelevisionScreenResolutions, LOCTEXT("CommonTelevesionsSectionHeader", "Televisions"));

	return MenuBuilder.MakeWidget();
}

void SDesignerView::BeginTransaction(const FText& SessionName)
{
	if ( ScopedTransaction == nullptr )
	{
		ScopedTransaction = new FScopedTransaction(SessionName);

		if ( SelectedWidget.IsValid() )
		{
			SelectedWidget.GetPreview()->Modify();
			SelectedWidget.GetTemplate()->Modify();
		}
	}
}

bool SDesignerView::InTransaction() const
{
	return ScopedTransaction != nullptr;
}

void SDesignerView::EndTransaction(bool bCancel)
{
	if ( ScopedTransaction != nullptr )
	{
		if ( bCancel )
		{
			ScopedTransaction->Cancel();
		}

		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

FReply SDesignerView::HandleZoomToFitClicked()
{
	ZoomToFit(/*bInstantZoom*/ false);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

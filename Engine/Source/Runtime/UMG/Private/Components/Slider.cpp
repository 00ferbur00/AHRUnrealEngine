// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// USlider

USlider::USlider(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	Orientation = EOrientation::Orient_Horizontal;
	SliderBarColor = FLinearColor::White;
	SliderHandleColor = FLinearColor::White;
}

TSharedRef<SWidget> USlider::RebuildWidget()
{
	MySlider = SNew(SSlider)
		.OnMouseCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureBegin))
		.OnMouseCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureEnd))
		.OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged));

	return MySlider.ToSharedRef();
}

void USlider::SyncronizeProperties()
{
	Super::SyncronizeProperties();

	TAttribute<float> ValueBinding = OPTIONAL_BINDING(float, Value);
	
	MySlider->SetOrientation(Orientation);
	MySlider->SetSliderBarColor(SliderBarColor);
	MySlider->SetSliderHandleColor(SliderHandleColor);
	MySlider->SetValue(ValueBinding);
}

void USlider::HandleOnValueChanged(float InValue)
{
	OnValueChanged.Broadcast(InValue);
}

void USlider::HandleOnMouseCaptureBegin()
{
	OnMouseCaptureBegin.Broadcast();
}

void USlider::HandleOnMouseCaptureEnd()
{
	OnMouseCaptureEnd.Broadcast();
}

float USlider::GetValue()
{
	return MySlider->GetValue();
}

void USlider::SetValue(float InValue)
{
	Value = InValue;
	return MySlider->SetValue(InValue);
}

#if WITH_EDITOR

const FSlateBrush* USlider::GetEditorIcon()
{
	return FUMGStyle::Get().GetBrush("Widget.Slider");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE

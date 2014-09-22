// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGPrivatePCH.h"

/////////////////////////////////////////////////////
// UVisual

UVisual::UVisual(const FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
}

void UVisual::ReleaseSlateResources(bool bReleaseChildren)
{
}

void UVisual::BeginDestroy()
{
	Super::BeginDestroy();

	const bool bReleaseChildren = false;
	ReleaseSlateResources(bReleaseChildren);
}
// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "IOSRuntimeSettingsPrivatePCH.h"

#include "IOSRuntimeSettings.h"

UIOSRuntimeSettings::UIOSRuntimeSettings(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	bEnableGameCenterSupport = true;
	bSupportsPortraitOrientation = true;
	BundleDisplayName = TEXT("UE4 Game");
	BundleName = TEXT("MyUE4Game");
	BundleIdentifier = TEXT("com.YourCompany.GameNameNoSpaces");
	VersionInfo = TEXT("1.0.0");
    FrameRateLock = EPowerUsageFrameRateLock::PUFRL_30;
}

#if WITH_EDITOR
void UIOSRuntimeSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Ensure that at least one orientation is supported
	if (!bSupportsPortraitOrientation && !bSupportsUpsideDownOrientation && !bSupportsLandscapeLeftOrientation && !bSupportsLandscapeRightOrientation)
	{
		bSupportsPortraitOrientation = true;
	}

	// Ensure that at least one API is supported
	if (!bSupportsMetal && !bSupportsOpenGLES2)
	{
		bSupportsOpenGLES2 = true;
	}
}
#endif

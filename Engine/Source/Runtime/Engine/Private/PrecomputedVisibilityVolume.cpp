// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"

APrecomputedVisibilityVolume::APrecomputedVisibilityVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BrushComponent->BodyInstance.bEnableCollision_DEPRECATED = false;
	BrushComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	bColored = true;
	BrushColor.R = 25;
	BrushColor.G = 255;
	BrushColor.B = 25;
	BrushColor.A = 255;

}

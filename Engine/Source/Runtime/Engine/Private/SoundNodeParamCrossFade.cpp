// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "EnginePrivate.h"
#include "SoundDefinitions.h"
#include "Sound/SoundNodeParamCrossFade.h"

/*-----------------------------------------------------------------------------
	USoundNodeParamCrossFade implementation.
-----------------------------------------------------------------------------*/
USoundNodeParamCrossFade::USoundNodeParamCrossFade(const class FPostConstructInitializeProperties& PCIP) : Super(PCIP)
{
}

float USoundNodeParamCrossFade::GetCurrentDistance(FAudioDevice* AudioDevice, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams) const
{
	float ParamValue = 0.0f;
	
	ActiveSound.GetFloatParameter(ParamName, ParamValue);
	return ParamValue;
}

bool USoundNodeParamCrossFade::AllowCrossfading(FActiveSound& ActiveSound) const
{
	// Always allow parameter to control crossfading, even on 2D/preview sounds
	return true;
}

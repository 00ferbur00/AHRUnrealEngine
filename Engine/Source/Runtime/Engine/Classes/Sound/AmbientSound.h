// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


// Base ambient sound actor

#pragma once
#include "AmbientSound.generated.h"

UCLASS(AutoExpandCategories=Audio, ClassGroup=Sounds, MinimalAPI, hidecategories(Collision, Input, Game), showcategories=("Input|MouseInput", "Input|TouchInput", "Game|Damage"))
class AAmbientSound : public AActor
{
	GENERATED_UCLASS_BODY()

	/** Should the audio component automatically play on load? */
	UPROPERTY()
	uint32 bAutoPlay_DEPRECATED:1;

	/** Audio component to play */
	UPROPERTY(EditInLine, Category=Sound, VisibleAnywhere, BlueprintReadOnly,meta=(ExposeFunctionCategories="Sound,Audio,Audio|Components|Audio"))
	TSubobjectPtr<class UAudioComponent> AudioComponent;
	
	/** Dummy sound node property to force instantiation of subobject.	*/
	UPROPERTY(instanced)
	class UDEPRECATED_SoundNodeAmbient* SoundNodeInstance_DEPRECATED;

	ENGINE_API static bool bUE4AudioRefactorMigrationUnderway;
	ENGINE_API void MigrateSoundNodeInstance();	
	ENGINE_API FString GetInternalSoundCueName();

	// Begin AActor interface.
#if WITH_EDITOR
	virtual void CheckForErrors() override;
	virtual bool GetReferencedContentObjects( TArray<UObject*>& Objects ) const override;
#endif
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	// End AActor interface.

	// BEGIN DEPRECATED (use component functions now in level script)
	UFUNCTION(BlueprintCallable, Category="Audio", meta=(DeprecatedFunction))
	void FadeIn(float FadeInDuration, float FadeVolumeLevel = 1.f);
	UFUNCTION(BlueprintCallable, Category="Audio", meta=(DeprecatedFunction))
	void FadeOut(float FadeOutDuration, float FadeVolumeLevel);
	UFUNCTION(BlueprintCallable, Category="Audio", meta=(DeprecatedFunction))
	void AdjustVolume(float AdjustVolumeDuration, float AdjustVolumeLevel);
	UFUNCTION(BlueprintCallable, Category="Audio", meta=(DeprecatedFunction))
	void Play(float StartTime = 0.f);
	UFUNCTION(BlueprintCallable, Category="Audio", meta=(DeprecatedFunction))
	void Stop();
	// END DEPRECATED
};




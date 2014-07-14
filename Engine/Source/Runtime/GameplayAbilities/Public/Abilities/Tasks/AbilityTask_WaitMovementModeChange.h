// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "AbilityTask.h"
#include "AbilityTask_WaitMovementModeChange.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMovementModeChangedDelegate, EMovementMode, NewMovementMode);

UCLASS(MinimalAPI)
class UAbilityTask_WaitMovementModeChange : public UAbilityTask
{
	GENERATED_UCLASS_BODY()

public:

	UPROPERTY(BlueprintAssignable)
	FMovementModeChangedDelegate	OnChange;

	UFUNCTION()
	void OnMovementModeChange(ACharacter * Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	EMovementMode	RequiredMode;

	UFUNCTION(BlueprintCallable, Category=Abilities, meta = (HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_WaitMovementModeChange* CreateWaitMovementModeChange(UObject* WorldContextObject, EMovementMode NewMode);

	virtual void Activate() override;
};
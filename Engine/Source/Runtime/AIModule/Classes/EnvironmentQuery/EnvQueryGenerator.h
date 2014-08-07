// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvQueryGenerator.generated.h"

UCLASS(Abstract)
class AIMODULE_API UEnvQueryGenerator : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditDefaultsOnly, Category=Option)
	FString OptionName;

	/** type of generated items */
	TSubclassOf<class UEnvQueryItemType> ItemType;

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const { checkNoEntry(); }

	/** get description of generator */
	virtual FText GetDescriptionTitle() const;
	virtual FText GetDescriptionDetails() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
};

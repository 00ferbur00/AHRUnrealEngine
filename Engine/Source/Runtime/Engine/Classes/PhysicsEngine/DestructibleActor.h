// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

/** This class represents an APEX Destructible Actor. */

#include "DestructibleActor.generated.h"

class UDestructibleComponent;

/** Delegate for notification when fracture occurs */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FActorFractureSignature, const FVector &, HitPoint, const FVector &, HitDirection);

UCLASS(MinimalAPI, hidecategories=(Input), showcategories=("Input|MouseInput", "Input|TouchInput"))
class ADestructibleActor : public AActor
{
	GENERATED_UCLASS_BODY()

	/**
	 * The component which holds the skinned mesh and physics data for this actor.
	 */
private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Destruction, meta = (ExposeFunctionCategories = "Destruction,Components|Destructible", AllowPrivateAccess = "true"))
	UDestructibleComponent* DestructibleComponent;
public:

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Navigation)
	uint32 bAffectNavigation : 1;

	UPROPERTY(BlueprintAssignable, Category = "Components|Destructible")
	FActorFractureSignature OnActorFracture;

	// Begin AActor interface.
#if WITH_EDITOR
	ENGINE_API virtual bool GetReferencedContentObjects( TArray<UObject*>& Objects ) const override;
#endif // WITH_EDITOR
	// End AActor interface.



public:
	/** Returns DestructibleComponent subobject **/
	FORCEINLINE UDestructibleComponent* GetDestructibleComponent() const { return DestructibleComponent; }
};




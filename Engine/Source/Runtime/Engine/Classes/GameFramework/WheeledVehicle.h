// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "GameFramework/Pawn.h"
#include "WheeledVehicle.generated.h"

class FDebugDisplayInfo;

UCLASS(abstract, config=Game, BlueprintType)
class ENGINE_API AWheeledVehicle : public APawn
{
	GENERATED_UCLASS_BODY()

private:
	/**  The main skeletal mesh associated with this Vehicle */
	UPROPERTY(Category = Vehicle, VisibleDefaultsOnly, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class USkeletalMeshComponent* Mesh;

	/** vehicle simulation component */
	UPROPERTY(Category = Vehicle, VisibleDefaultsOnly, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UWheeledVehicleMovementComponent* VehicleMovement;
public:

	/** Name of the MeshComponent. Use this name if you want to prevent creation of the component (with ObjectInitializer.DoNotCreateDefaultSubobject). */
	static FName VehicleMeshComponentName;

	/** Name of the VehicleMovement. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName VehicleMovementComponentName;

	/** Util to get the wheeled vehicle movement component */
	class UWheeledVehicleMovementComponent* GetVehicleMovementComponent() const 
	{ 
		return VehicleMovement; 
	}

	// Begin AActor interface
	virtual void DisplayDebug(class UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
	// End Actor interface

	/** Returns Mesh subobject **/
	FORCEINLINE class USkeletalMeshComponent* GetMesh() const { return Mesh; }
	/** Returns VehicleMovement subobject **/
	FORCEINLINE class UWheeledVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }
};

// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/**
 * A sticky note.  Level designers can place these in the level and then
 * view them as a batch in the error/warnings window.
 */

#pragma once
#include "Note.generated.h"

UCLASS(MinimalAPI, hidecategories = (Input), showcategories=("Input|MouseInput", "Input|TouchInput"))
class ANote : public AActor
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Note)
	FString Text;

	// Reference to sprite visualization component
private:
	UPROPERTY()
	class UBillboardComponent* SpriteComponent;

	// Reference to arrow visualization component
	UPROPERTY()
	class UArrowComponent* ArrowComponent;
public:

#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	// Begin AActor Interface
	virtual void CheckForErrors();
	// End AActor Interface
#endif

public:
#if WITH_EDITORONLY_DATA
	/** Returns SpriteComponent subobject **/
	FORCEINLINE class UBillboardComponent* GetSpriteComponent() const { return SpriteComponent; }
	/** Returns ArrowComponent subobject **/
	FORCEINLINE class UArrowComponent* GetArrowComponent() const { return ArrowComponent; }
#endif
};




// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "BTFunctionLibrary.generated.h"

class UBlackboardComponent;

UCLASS(meta=(RestrictedToClasses="BTNode"))
class AIMODULE_API UBTFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static UBlackboardComponent* GetOwnersBlackboard(UBTNode* NodeOwner);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static UObject* GetBlackboardValueAsObject(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static AActor* GetBlackboardValueAsActor(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static UClass* GetBlackboardValueAsClass(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static uint8 GetBlackboardValueAsEnum(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static int32 GetBlackboardValueAsInt(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static float GetBlackboardValueAsFloat(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static bool GetBlackboardValueAsBool(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static FString GetBlackboardValueAsString(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static FName GetBlackboardValueAsName(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintPure, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static FVector GetBlackboardValueAsVector(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsObject(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, UObject* Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsClass(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, UClass* Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsEnum(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, uint8 Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsInt(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, int32 Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsFloat(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, float Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsBool(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, bool Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsString(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, FString Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsName(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, FName Value);

	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void SetBlackboardValueAsVector(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key, FVector Value);

	UFUNCTION(BlueprintCallable, Category=BehaviorTree, Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner"))
	static void ClearBlackboardValueAsVector(UBTNode* NodeOwner, const struct FBlackboardKeySelector& Key);

	/** Initialize variables marked as "instance memory" and set owning actor for blackboard operations */
	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner", DeprecatedFunction, DeprecationMessage="No longer needed"))
	static void StartUsingExternalEvent(UBTNode* NodeOwner, AActor* OwningActor);

	/** Save variables marked as "instance memory" and clear owning actor */
	UFUNCTION(BlueprintCallable, Category="AI|BehaviorTree", Meta=(HidePin="NodeOwner", DefaultToSelf="NodeOwner", DeprecatedFunction, DeprecationMessage="No longer needed"))
	static void StopUsingExternalEvent(UBTNode* NodeOwner);
};

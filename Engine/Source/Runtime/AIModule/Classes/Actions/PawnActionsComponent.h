// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "GameFramework/Pawn.h"
#include "PawnActionsComponent.generated.h"

class UPawnAction;

USTRUCT()
struct FPawnActionEvent
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UPawnAction* Action;

	EPawnActionEventType::Type EventType;

	EAIRequestPriority::Type Priority;

	// used to maintain order of equally-important messages
	uint32 Index;

	FPawnActionEvent() : Action(NULL), EventType(EPawnActionEventType::Invalid), Priority(EAIRequestPriority::MAX), Index(uint32(-1))
	{}

	FPawnActionEvent(UPawnAction* Action, EPawnActionEventType::Type EventType, uint32 Index);

	bool operator==(const FPawnActionEvent& Other) const { return (Action == Other.Action) && (EventType == Other.EventType) && (Priority == Other.Priority); }
};

USTRUCT()
struct FPawnActionStack
{
	GENERATED_USTRUCT_BODY()

private:
	UPROPERTY()
	UPawnAction* TopAction;

public:
	void Pause();
	void Resume();

	/** All it does is tie actions into a double-linked list making NewTopAction
	 *	new stack's top */
	void PushAction(UPawnAction* NewTopAction);

	/** Looks through the double-linked action list looking for specified action
	 *	and if found action will be popped along with all it's siblings */
	void PopAction(UPawnAction* ActionToPop);
	
	FORCEINLINE UPawnAction* GetTop() { return TopAction; }
	FORCEINLINE const UPawnAction* GetTop() const { return TopAction; }
	FORCEINLINE bool IsEmpty() const { return TopAction == NULL; }

	//----------------------------------------------------------------------//
	// Debugging-testing purposes 
	//----------------------------------------------------------------------//
	int32 GetStackSize() const;
};

UCLASS()
class AIMODULE_API UPawnActionsComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

protected:
	UPROPERTY(BlueprintReadOnly, Category="PawnActions")
	APawn* ControlledPawn;

	UPROPERTY()
	TArray<FPawnActionStack> ActionStacks;

	UPROPERTY()
	TArray<FPawnActionEvent> ActionEvents;

	UPROPERTY(Transient)
	UPawnAction* CurrentAction;

	/** set when logic was locked by hi priority stack */
	uint32 bLockedAILogic : 1;

private:
	uint32 ActionEventIndex;

public:
	//----------------------------------------------------------------------//
	// blueprint interface
	//----------------------------------------------------------------------//

	UFUNCTION(BlueprintCallable, Category = "AI|PawnActions")
	static bool PerformAction(APawn* Pawn, UPawnAction* Action, TEnumAsByte<EAIRequestPriority::Type> Priority = EAIRequestPriority::HardScript);

	//----------------------------------------------------------------------//
	// 
	//----------------------------------------------------------------------//
	/** Use it to save component work to figure out what it's controlling
	 *	or if component can't/won't be able to figure it out properly
	 *	@NOTE will throw a log warning if trying to set ControlledPawn if it's already set */
	void SetControlledPawn(APawn* NewPawn);
	FORCEINLINE APawn* GetControlledPawn() { return ControlledPawn; }
	FORCEINLINE const APawn* GetControlledPawn() const { return ControlledPawn; }
	FORCEINLINE AController* GetController() { return ControlledPawn ? ControlledPawn->GetController() : NULL; }
	FORCEINLINE UPawnAction* GetCurrentAction() { return CurrentAction; }

	bool OnEvent(UPawnAction* Action, EPawnActionEventType::Type Event);

	UFUNCTION(BlueprintCallable, Category = PawnAction)
	bool PushAction(UPawnAction* NewAction, EAIRequestPriority::Type Priority, UObject* Instigator = NULL);

	/** Aborts given action instance */
	UFUNCTION(BlueprintCallable, Category = PawnAction)
	bool AbortAction(UPawnAction* ActionToAbort);

	/** Aborts given action instance */
	UFUNCTION(BlueprintCallable, Category = PawnAction)
	bool ForceAbortAction(UPawnAction* ActionToAbort);

	/** removes all actions instigated with Priority by Instigator
	 *	@param Priority if equal to EAIRequestPriority::MAX then all priority queues will be searched. 
	 *		This is less efficient so use with caution 
	 *	@return number of action abortions requested (performed asyncronously) */
	uint32 AbortActionsInstigatedBy(UObject* const Instigator, EAIRequestPriority::Type Priority);
	
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	FORCEINLINE UPawnAction* GetActiveAction(EAIRequestPriority::Type Priority) { return ActionStacks[Priority].GetTop(); }

#if ENABLE_VISUAL_LOG
	void DescribeSelfToVisLog(struct FVisLogEntry* Snapshot) const;
#endif // ENABLE_VISUAL_LOG

	//----------------------------------------------------------------------//
	// Debugging-testing purposes 
	//----------------------------------------------------------------------//
	int32 GetActionStackSize(EAIRequestPriority::Type Priority) const { return ActionStacks[Priority].GetStackSize(); }
	int32 GetActionEventsQueueSize() const { return ActionEvents.Num(); }

protected:
	/** Finds the action that should be running. If it's different from CurrentAction
	 *	then CurrentAction gets paused and newly selected action gets started up */
	void UpdateCurrentAction();

	APawn* CacheControlledPawn();

	void UpdateAILogicLock();
};
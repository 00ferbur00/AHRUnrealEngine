// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AITestSuitePrivatePCH.h"
#include "BehaviorTree/TestBTDecorator_DelayedAbort.h"

UTestBTDecorator_DelayedAbort::UTestBTDecorator_DelayedAbort(const FPostConstructInitializeProperties& PCIP) : Super(PCIP)
{
	NodeName = "Delayed Abort";
	DelayTicks = 5;
	bOnlyOnce = true;
	
	bNotifyTick = true;
	bNotifyBecomeRelevant = true;

	bAllowAbortNone = false;
	bAllowAbortLowerPri = false;
	bAllowAbortChildNodes = true;
	FlowAbortMode = EBTFlowAbortMode::Self;
}

void UTestBTDecorator_DelayedAbort::OnBecomeRelevant(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory)
{
	FBTDelayedAbortMemory* MyMemory = (FBTDelayedAbortMemory*)NodeMemory;
	MyMemory->EndFrameIdx = GFrameCounter + DelayTicks;
}

void UTestBTDecorator_DelayedAbort::TickNode(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTDelayedAbortMemory* MyMemory = (FBTDelayedAbortMemory*)NodeMemory;

	if (GFrameCounter >= MyMemory->EndFrameIdx)
	{
		OwnerComp->RequestExecution(this);
		MyMemory->EndFrameIdx = bOnlyOnce ? MAX_uint64 : 0;
	}
}

uint16 UTestBTDecorator_DelayedAbort::GetInstanceMemorySize() const
{
	return sizeof(FBTDelayedAbortMemory);
}
// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AIModulePrivate.h"
#include "BlueprintNodeHelpers.h"
#include "BehaviorTree/Services/BTService_BlueprintBase.h"

UBTService_BlueprintBase::UBTService_BlueprintBase(const FPostConstructInitializeProperties& PCIP) : Super(PCIP)
{
	UClass* StopAtClass = UBTService_BlueprintBase::StaticClass();
	bImplementsReceiveTick = BlueprintNodeHelpers::HasBlueprintFunction(TEXT("ReceiveTick"), this, StopAtClass);
	bImplementsReceiveActivation = BlueprintNodeHelpers::HasBlueprintFunction(TEXT("ReceiveActivation"), this, StopAtClass);
	bImplementsReceiveDeactivation = BlueprintNodeHelpers::HasBlueprintFunction(TEXT("ReceiveDeactivation"), this, StopAtClass);
	bImplementsReceiveSearchStart = BlueprintNodeHelpers::HasBlueprintFunction(TEXT("ReceiveSearchStart"), this, StopAtClass);

	bNotifyBecomeRelevant = bImplementsReceiveActivation;
	bNotifyCeaseRelevant = bNotifyBecomeRelevant;
	bNotifyOnSearch = bImplementsReceiveTick || bImplementsReceiveSearchStart;
	bNotifyTick = bImplementsReceiveTick;
	bShowPropertyDetails = true;

	// all blueprint based nodes must create instances
	bCreateNodeInstance = true;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		BlueprintNodeHelpers::CollectPropertyData(this, StopAtClass, PropertyData);
	}
}

void UBTService_BlueprintBase::PostInitProperties()
{
	Super::PostInitProperties();
	NodeName = BlueprintNodeHelpers::GetNodeName(this);
}

void UBTService_BlueprintBase::OnBecomeRelevant(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	// check flag, it could be used because user wants tick
	if (bImplementsReceiveActivation)
	{
		ReceiveActivation(OwnerComp->GetOwner());
	}
}

void UBTService_BlueprintBase::OnCeaseRelevant(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory)
{
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);

	if (OwnerComp && !OwnerComp->HasAnyFlags(RF_BeginDestroyed) && OwnerComp->GetOwner())
	{
		// force dropping all pending latent actions associated with this blueprint
		// we can't have those resuming activity when node is/was aborted
		BlueprintNodeHelpers::AbortLatentActions(OwnerComp, this);

		if (bImplementsReceiveDeactivation)
		{
			ReceiveDeactivation(OwnerComp->GetOwner());
		}
	}
	else
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("OnCeaseRelevant called on Blueprint service %s with invalid owner.  OwnerComponent: %s, OwnerComponent Owner: %s.  %s"),
			*GetNameSafe(this),
			*GetNameSafe(OwnerComp),
			OwnerComp ? *GetNameSafe(OwnerComp->GetOwner()) : TEXT("<None>"),
			OwnerComp && OwnerComp->HasAnyFlags(RF_BeginDestroyed) ? TEXT("OwnerComponent has BeginDestroyed flag") : TEXT("")
			  );
	}
}

void UBTService_BlueprintBase::OnSearchStart(struct FBehaviorTreeSearchData& SearchData)
{
	// skip flag, will be handled by bNotifyOnSearch

	if (bImplementsReceiveSearchStart)
	{
		ReceiveSearchStart(SearchData.OwnerComp->GetOwner());
	}
	else
	{
		Super::OnSearchStart(SearchData);
	}
}

void UBTService_BlueprintBase::TickNode(UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	// skip flag, will be handled by bNotifyTick

	ReceiveTick(OwnerComp->GetOwner(), DeltaSeconds);
}

bool UBTService_BlueprintBase::IsServiceActive() const
{
	UBehaviorTreeComponent* OwnerComp = Cast<UBehaviorTreeComponent>(GetOuter());
	const bool bIsActive = OwnerComp->IsAuxNodeActive(this);
	return bIsActive;
}

FString UBTService_BlueprintBase::GetStaticServiceDescription() const
{
	FString ReturnDesc;

	UBTService_BlueprintBase* CDO = (UBTService_BlueprintBase*)(GetClass()->GetDefaultObject());
	if (CDO)
	{
		ReturnDesc = FString::Printf(TEXT("%s, %s, %s, %s\n"),
			bImplementsReceiveTick ? *GetStaticTickIntervalDescription() : TEXT("No tick"),
			bImplementsReceiveActivation ? TEXT("Activation") : TEXT("No Activation"),
			bImplementsReceiveDeactivation ? TEXT("Deactivation") : TEXT("No Deactivation"),
			bImplementsReceiveSearchStart ? TEXT("Search Start") : TEXT("No Search Start"));
								
		if (bShowPropertyDetails)
		{
			UClass* StopAtClass = UBTService_BlueprintBase::StaticClass();
			FString PropertyDesc = BlueprintNodeHelpers::CollectPropertyDescription(this, StopAtClass, CDO->PropertyData);
			if (PropertyDesc.Len())
			{
				ReturnDesc += TEXT("\n");
				ReturnDesc += PropertyDesc;
			}
		}
	}

	return ReturnDesc;
}

void UBTService_BlueprintBase::DescribeRuntimeValues(const UBehaviorTreeComponent* OwnerComp, uint8* NodeMemory, EBTDescriptionVerbosity::Type Verbosity, TArray<FString>& Values) const
{
	UBTService_BlueprintBase* CDO = (UBTService_BlueprintBase*)(GetClass()->GetDefaultObject());
	if (CDO && CDO->PropertyData.Num())
	{
		UClass* StopAtClass = UBTService_BlueprintBase::StaticClass();
		BlueprintNodeHelpers::DescribeRuntimeValues(this, CDO->PropertyData, Values);
	}
}

void UBTService_BlueprintBase::OnInstanceDestroyed(UBehaviorTreeComponent* OwnerComp)
{
	// force dropping all pending latent actions associated with this blueprint
	BlueprintNodeHelpers::AbortLatentActions(OwnerComp, this);
}

#if WITH_EDITOR

bool UBTService_BlueprintBase::UsesBlueprint() const
{
	return true;
}

#endif // WITH_EDITOR
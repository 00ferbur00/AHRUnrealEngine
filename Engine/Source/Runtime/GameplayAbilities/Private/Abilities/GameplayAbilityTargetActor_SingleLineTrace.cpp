// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemPrivatePCH.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilityTargetActor_SingleLineTrace.h"
#include "Engine/World.h"

// --------------------------------------------------------------------------------------------------------------------------------------------------------
//
//	AGameplayAbilityTargetActor_SingleLineTrace
//
// --------------------------------------------------------------------------------------------------------------------------------------------------------

AGameplayAbilityTargetActor_SingleLineTrace::AGameplayAbilityTargetActor_SingleLineTrace(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	StaticTargetFunction = false;
	bDebug = false;
	bBindToConfirmCancelInputs = true;
}

void AGameplayAbilityTargetActor_SingleLineTrace::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGameplayAbilityTargetActor_SingleLineTrace, bDebug);
	DOREPLIFETIME(AGameplayAbilityTargetActor_SingleLineTrace, SourceActor);
}


FHitResult AGameplayAbilityTargetActor_SingleLineTrace::PerformTrace(AActor *SourceActor)
{
	static const FName LineTraceSingleName(TEXT("AGameplayAbilityTargetActor_SingleLineTrace"));
	bool bTraceComplex = false;
	TArray<AActor*> ActorsToIgnore;

	ActorsToIgnore.Add(SourceActor);

	FCollisionQueryParams Params(LineTraceSingleName, bTraceComplex);
	Params.bReturnPhysicalMaterial = true;
	Params.bTraceAsyncScene = true;
	Params.AddIgnoredActors(ActorsToIgnore);

	FVector TraceStart = SourceActor->GetActorLocation();
	FVector TraceEnd = TraceStart + (SourceActor->GetActorForwardVector() * 3000.f);

	// ------------------------------------------------------

	FHitResult ReturnHitResult;
	SourceActor->GetWorld()->LineTraceSingle(ReturnHitResult, TraceStart, TraceEnd, ECC_WorldStatic, Params);
	return ReturnHitResult;
}

FGameplayAbilityTargetDataHandle AGameplayAbilityTargetActor_SingleLineTrace::StaticGetTargetData(UWorld * World, const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo)
{
	SourceActor = ActorInfo->Actor.Get();
	check(SourceActor.Get());

	FGameplayAbilityTargetData_SingleTargetHit* ReturnData = new FGameplayAbilityTargetData_SingleTargetHit();
	ReturnData->HitResult = PerformTrace(SourceActor.Get());
	return FGameplayAbilityTargetDataHandle(ReturnData);
}

void AGameplayAbilityTargetActor_SingleLineTrace::StartTargeting(UGameplayAbility* InAbility)
{
	Ability = InAbility;

	// We can bind directly to our ASC's confirm/cancel events, or wait to be told from an outside source to confirm or cancel
	if (bBindToConfirmCancelInputs)
	{
		UAbilitySystemComponent* ASC = Ability->GetCurrentActorInfo()->AbilitySystemComponent.Get();
		if (ASC)
		{
			ASC->ConfirmCallbacks.AddDynamic(this, &AGameplayAbilityTargetActor_SingleLineTrace::Confirm);
			ASC->CancelCallbacks.AddDynamic(this, &AGameplayAbilityTargetActor_SingleLineTrace::Cancel);
		}
	}
	
	bDebug = true;
}

void AGameplayAbilityTargetActor_SingleLineTrace::Confirm()
{
	if (Ability.IsValid())
	{
		bDebug = false;
		FGameplayAbilityTargetDataHandle Handle = StaticGetTargetData(Ability->GetWorld(), Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo());
		TargetDataReadyDelegate.Broadcast(Handle);
	}

	Destroy();
}

void AGameplayAbilityTargetActor_SingleLineTrace::Cancel()
{
	CanceledDelegate.Broadcast(FGameplayAbilityTargetDataHandle());
	Destroy();
}

void AGameplayAbilityTargetActor_SingleLineTrace::Tick(float DeltaSeconds)
{
	//Super::Tick(DeltaSeconds);
	FGameplayAbilityTargetDataHandle Handle;
	FHitResult HitResult;

	/** Temp: Do a trace wiuth bDebug=true to draw a cheap "preview" */
	if (Ability.IsValid())
	{
		Super::Tick(DeltaSeconds);
		Handle = StaticGetTargetData(Ability->GetWorld(), Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo());
		HitResult = *Handle.Data->GetHitResult();
	}
	else
	{
		Super::Tick(DeltaSeconds);
	}

	// very temp - do a mostly hardcoded trace from the source actor
	if (bDebug && SourceActor.Get())
	{
		if (!Ability.IsValid())
		{
			HitResult = PerformTrace(SourceActor.Get());
		}
		DrawDebugLine(GetWorld(), SourceActor->GetActorLocation(), HitResult.Location, FLinearColor::Green, false);
		DrawDebugSphere(GetWorld(), HitResult.Location, 16, 10, FLinearColor::Green, false);
	}
}

void AGameplayAbilityTargetActor_SingleLineTrace::ConfirmTargeting()
{
	if (Ability.IsValid())
	{
		bDebug = false;
		FGameplayAbilityTargetDataHandle Handle = StaticGetTargetData(Ability->GetWorld(), Ability->GetCurrentActorInfo(), Ability->GetCurrentActivationInfo());
		TargetDataReadyDelegate.Broadcast(Handle);
	}

	Destroy();
}


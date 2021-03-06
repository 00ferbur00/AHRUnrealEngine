// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemPrivatePCH.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayTagsModule.h"
#include "GameplayCueManager.h"
#include "AbilitySystemComponent.h"

UGameplayCueNotify_Static::UGameplayCueNotify_Static(const FObjectInitializer& PCIP)
: Super(PCIP)
{
	IsOverride = true;
}

#if WITH_EDITOR
void UGameplayCueNotify_Static::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UAbilitySystemGlobals::Get().GetGameplayCueManager()->bAccelerationMapOutdated = true;
}
#endif

void UGameplayCueNotify_Static::DeriveGameplayCueTagFromAssetName()
{
	UAbilitySystemGlobals::DeriveGameplayCueTagFromAssetName(GetName(), GameplayCueTag, GameplayCueName);
}

void UGameplayCueNotify_Static::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		DeriveGameplayCueTagFromAssetName();
	}

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		DeriveGameplayCueTagFromAssetName();
	}
}

void UGameplayCueNotify_Static::PostInitProperties()
{
	Super::PostInitProperties();
	DeriveGameplayCueTagFromAssetName();
}

bool UGameplayCueNotify_Static::HandlesEvent(EGameplayCueEvent::Type EventType) const
{
	return true;
}

void UGameplayCueNotify_Static::HandleGameplayCue(AActor* MyTarget, EGameplayCueEvent::Type EventType, FGameplayCueParameters Parameters)
{
	if (MyTarget && !MyTarget->IsPendingKill())
	{
		K2_HandleGameplayCue(MyTarget, EventType, Parameters);

		switch (EventType)
		{
		case EGameplayCueEvent::OnActive:
			OnActive(MyTarget, Parameters);
			break;

		case EGameplayCueEvent::Executed:
			OnExecute(MyTarget, Parameters);
			break;

		case EGameplayCueEvent::Removed:
			OnRemove(MyTarget, Parameters);
			break;
		};
	}
	else
	{
		ABILITY_LOG(Warning, TEXT("Null Target"));
	}
}

void UGameplayCueNotify_Static::OnOwnerDestroyed()
{
}

bool UGameplayCueNotify_Static::OnExecute_Implementation(AActor* MyTarget, FGameplayCueParameters Parameters) const
{
	return false;
}

bool UGameplayCueNotify_Static::OnActive_Implementation(AActor* MyTarget, FGameplayCueParameters Parameters) const
{
	return false;
}

bool UGameplayCueNotify_Static::OnRemove_Implementation(AActor* MyTarget, FGameplayCueParameters Parameters) const
{
	return false;
}

// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "AbilitySystemPrivatePCH.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitMovementModeChange.h"
#include "Abilities/Tasks/AbilityTask_WaitOverlap.h"
#include "Abilities/Tasks/AbilityTask_WaitConfirmCancel.h"
#include "LatentActions.h"

UAbilitySystemBlueprintLibrary::UAbilitySystemBlueprintLibrary(const class FPostConstructInitializeProperties& PCIP)
: Super(PCIP)
{
}

UAbilitySystemComponent* UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(AActor *Actor)
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
}

FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::AppendTargetDataHandle(FGameplayAbilityTargetDataHandle TargetHandle, FGameplayAbilityTargetDataHandle HandleToAdd)
{
	TargetHandle.Append(&HandleToAdd);
	return TargetHandle;
}

FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::AbilityTargetDataFromLocations(const FGameplayAbilityTargetingLocationInfo& SourceLocation, const FGameplayAbilityTargetingLocationInfo& TargetLocation)
{
	// Construct TargetData
	FGameplayAbilityTargetData_LocationInfo*	NewData = new FGameplayAbilityTargetData_LocationInfo();
	NewData->SourceLocation = SourceLocation;
	NewData->TargetLocation = TargetLocation;

	// Give it a handle and return
	FGameplayAbilityTargetDataHandle	Handle;
	Handle.Data.Add(TSharedPtr<FGameplayAbilityTargetData_LocationInfo>(NewData));
	return Handle;
}

FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::AbilityTargetDataHandleFromAbilityTargetDataMesh(FGameplayAbilityTargetData_Mesh Data)
{
	// Construct TargetData
	FGameplayAbilityTargetData_Mesh*	NewData = new FGameplayAbilityTargetData_Mesh(Data);
	FGameplayAbilityTargetDataHandle	Handle;

	// Give it a handle and return
	Handle.Data.Add(TSharedPtr<FGameplayAbilityTargetData_Mesh>(NewData));
	return Handle;
}

FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(AActor* Actor)
{
	// Construct TargetData
	FGameplayAbilityTargetData_ActorArray*	NewData = new FGameplayAbilityTargetData_ActorArray();
	NewData->TargetActorArray.Add(Actor);
	FGameplayAbilityTargetDataHandle		Handle(NewData);
	return Handle;
}

FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActorArray(TArray<TWeakObjectPtr<AActor>> ActorArray, bool OneTargetPerHandle)
{
	// Construct TargetData
	if (OneTargetPerHandle)
	{
		FGameplayAbilityTargetDataHandle		Handle;
		for (int32 i = 0; i < ActorArray.Num(); ++i)
		{
			if (ActorArray[i].IsValid())
			{
				FGameplayAbilityTargetDataHandle TempHandle = AbilityTargetDataFromActor(ActorArray[i].Get());
				Handle.Append(&TempHandle);
			}
		}
		return Handle;
	}
	else
	{
		FGameplayAbilityTargetData_ActorArray*	NewData = new FGameplayAbilityTargetData_ActorArray();
		NewData->TargetActorArray = ActorArray;
		FGameplayAbilityTargetDataHandle		Handle(NewData);
		return Handle;
	}
}


FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::FilterTargetData(FGameplayAbilityTargetDataHandle TargetDataHandle, FGameplayTargetDataFilterHandle FilterHandle)
{
	FGameplayAbilityTargetDataHandle ReturnDataHandle;
	
	//return ReturnDataHandle;
	check(FilterHandle.Filter.IsValid());		//Return unfiltered data

	for (int32 i = 0; TargetDataHandle.IsValid(i); ++i)
	{
		FGameplayAbilityTargetData* UnfilteredData = TargetDataHandle.Get(i);
		check(UnfilteredData);
		if (UnfilteredData->GetActors().Num() > 0)
		{
			//TArray<TWeakObjectPtr<AActor>> FilteredActors = FilterActorArray(UnfilteredData->GetActors(), Filter);
			TArray<TWeakObjectPtr<AActor>> FilteredActors = UnfilteredData->GetActors().FilterByPredicate(*FilterHandle.Filter.Get());
			if (FilteredActors.Num() > 0)
			{
				//Copy the data first, since we don't understand the internals of it
				UScriptStruct* ScriptStruct = UnfilteredData->GetScriptStruct();
				FGameplayAbilityTargetData* NewData = (FGameplayAbilityTargetData*)FMemory::Malloc(ScriptStruct->GetCppStructOps()->GetSize());
				ScriptStruct->InitializeScriptStruct(NewData);
				ScriptStruct->CopyScriptStruct(NewData, UnfilteredData);
				ReturnDataHandle.Data.Add(TSharedPtr<FGameplayAbilityTargetData>(NewData));
				if (FilteredActors.Num() < UnfilteredData->GetActors().Num())
				{
					//We have lost some, but not all, of our actors, so replace the array. This should only be possible with targeting types that permit actor-array setting.
					if (!NewData->SetActors(FilteredActors))
					{
						//This is an error, though we could ignore it. We somehow filtered out part of a list, but the class doesn't support changing the list, so now it's all or nothing.
						check(false);
					}
				}
			}
		}
	}

	return ReturnDataHandle;
}

FGameplayTargetDataFilterHandle UAbilitySystemBlueprintLibrary::MakeFilterHandle(FGameplayTargetDataFilter Filter)
{
	FGameplayTargetDataFilterHandle FilterHandle;
	FGameplayTargetDataFilter* NewFilter = new FGameplayTargetDataFilter(Filter);
	FilterHandle.Filter = TSharedPtr<FGameplayTargetDataFilter>(NewFilter);
	return FilterHandle;
}


/*
TArray<TWeakObjectPtr<AActor>> UGameplayAbility::FilterActorArray(TArray<TWeakObjectPtr<AActor>> ArrayToFilter, FGameplayAbilityTargetDataActorFilter Filter)
{
	TArray<TWeakObjectPtr<AActor>> ReturnArray;
	int32 NewArraySize = 0;
	for (int32 i = 0; i < ArrayToFilter.Num(); ++i)
	{
		if (ArrayToFilter[i].IsValid())
		{
			if (Filter.FilterPassesForActor(ArrayToFilter[i].Get()))
			{
				++NewArraySize;
			}
		}
	}
	if (NewArraySize)
	{
		ReturnArray.Reserve(NewArraySize);
		for (int32 i = 0; i < ArrayToFilter.Num(); ++i)
		{
			if (ArrayToFilter[i].IsValid())
			{
				if (Filter.FilterPassesForActor(ArrayToFilter[i].Get()))
				{
					ReturnArray.Add(ArrayToFilter[i].Get());
				}
			}
		}
	}
	return ReturnArray;
}
*/

FGameplayAbilityTargetDataHandle UAbilitySystemBlueprintLibrary::AbilityTargetDataFromHitResult(FHitResult HitResult)
{
	// Construct TargetData
	FGameplayAbilityTargetData_SingleTargetHit* TargetData = new FGameplayAbilityTargetData_SingleTargetHit(HitResult);

	// Give it a handle and return
	FGameplayAbilityTargetDataHandle	Handle;
	Handle.Data.Add(TSharedPtr<FGameplayAbilityTargetData>(TargetData));

	return Handle;
}

int32 UAbilitySystemBlueprintLibrary::GetDataCountFromTargetData(FGameplayAbilityTargetDataHandle TargetData)
{
	return TargetData.Data.Num();
}

TArray<AActor*> UAbilitySystemBlueprintLibrary::GetActorsFromTargetData(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	if (TargetData.Data.IsValidIndex(Index))
	{
		FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
		TArray<AActor*>	ResolvedArray;
		if (Data)
		{
			TArray<TWeakObjectPtr<AActor> > WeakArray = Data->GetActors();
			for (TWeakObjectPtr<AActor> WeakPtr : WeakArray)
			{
				ResolvedArray.Add(WeakPtr.Get());
			}
		}
		return ResolvedArray;
	}
	return TArray<AActor*>();
}

bool UAbilitySystemBlueprintLibrary::TargetDataHasHitResult(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	if (TargetData.Data.IsValidIndex(Index))
	{
		FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
		if (Data)
		{
			return Data->HasHitResult();
		}
	}
	return false;
}

FHitResult UAbilitySystemBlueprintLibrary::GetHitResultFromTargetData(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	if (TargetData.Data.IsValidIndex(Index))
	{
		FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
		if (Data)
		{
			const FHitResult* HitResultPtr = Data->GetHitResult();
			if (HitResultPtr)
			{
				return *HitResultPtr;
			}
		}
	}

	return FHitResult();
}

bool UAbilitySystemBlueprintLibrary::TargetDataHasOrigin(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
	if (Data)
	{
		return (Data->HasHitResult() || Data->HasOrigin());
	}
	return false;
}

FTransform UAbilitySystemBlueprintLibrary::GetTargetDataOrigin(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
	if (Data)
	{
		if (Data->HasOrigin())
		{
			return Data->GetOrigin();
		}
		if (Data->HasHitResult())
		{
			const FHitResult* HitResultPtr = Data->GetHitResult();
			FTransform ReturnTransform;
			ReturnTransform.SetLocation(HitResultPtr->TraceStart);
			ReturnTransform.SetRotation((HitResultPtr->Location - HitResultPtr->TraceStart).SafeNormal().Rotation().Quaternion());
			return ReturnTransform;
		}
	}

	return FTransform::Identity;
}

bool UAbilitySystemBlueprintLibrary::TargetDataHasEndPoint(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	if (TargetData.Data.IsValidIndex(Index))
	{
		FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
		if (Data)
		{
			return (Data->HasHitResult() || Data->HasEndPoint());
		}
	}
	return false;
}

FVector UAbilitySystemBlueprintLibrary::GetTargetDataEndPoint(FGameplayAbilityTargetDataHandle TargetData, int32 Index)
{
	if (TargetData.Data.IsValidIndex(Index))
	{
		FGameplayAbilityTargetData* Data = TargetData.Data[Index].Get();
		if (Data)
		{
			const FHitResult* HitResultPtr = Data->GetHitResult();
			if (HitResultPtr)
			{
				return HitResultPtr->Location;
			}
			else if (Data->HasEndPoint())
			{
				return Data->GetEndPoint();
			}
		}
	}

	return FVector::ZeroVector;
}

// -------------------------------------------------------------------------------------


bool UAbilitySystemBlueprintLibrary::IsInstigatorLocallyControlled(FGameplayCueParameters Parameters)
{
	return Parameters.InstigatorContext.IsLocallyControlled();
}


FHitResult UAbilitySystemBlueprintLibrary::GetHitResult(FGameplayCueParameters Parameters)
{
	if (Parameters.InstigatorContext.HitResult.IsValid())
	{
		return *Parameters.InstigatorContext.HitResult.Get();
	}
	
	return FHitResult();
}

bool UAbilitySystemBlueprintLibrary::HasHitResult(FGameplayCueParameters Parameters)
{
	return Parameters.InstigatorContext.HitResult.IsValid();
}

AActor*	UAbilitySystemBlueprintLibrary::GetInstigatorActor(FGameplayCueParameters Parameters)
{
	return Parameters.InstigatorContext.GetOriginalInstigator();
}

FTransform UAbilitySystemBlueprintLibrary::GetInstigatorTransform(FGameplayCueParameters Parameters)
{
	AActor* InstigatorActor = GetInstigatorActor(Parameters);
	if (InstigatorActor)
	{
		return InstigatorActor->GetTransform();
	}

	ABILITY_LOG(Warning, TEXT("UAbilitySystemBlueprintLibrary::GetInstigatorTransform called on GameplayCue with no valid instigator"));
	return FTransform::Identity;
}
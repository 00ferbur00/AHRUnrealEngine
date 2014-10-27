// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISenseImplementation.h"
#include "AISenseImplementation_Team.generated.h"

USTRUCT()
struct AIMODULE_API FAITeamStimulusEvent
{	
	GENERATED_USTRUCT_BODY()

	typedef class UAISenseImplementation_Team FSenseClass;

	FVector LastKnowLocation;
private:
	FVector BroadcastLocation;
public:
	float RangeSq;
	float InformationAge;
	FGenericTeamId TeamIdentifier;
private:
	UPROPERTY()
	class AActor* Broadcaster;
public:
	UPROPERTY()
	class AActor* Enemy;
		
	FAITeamStimulusEvent(){}	
	FAITeamStimulusEvent(class AActor* InBroadcaster, class AActor* InEnemy, const FVector& InLastKnowLocation, float EventRange, float PassedInfoAge = 0.f);

	FORCEINLINE void CacheBroadcastLocation()
	{
		BroadcastLocation = Broadcaster ? Broadcaster->GetActorLocation() : FAISystem::InvalidLocation;
	}

	FORCEINLINE const FVector& GetBroadcastLocation() const 
	{
		return BroadcastLocation;
	}
};

UCLASS(ClassGroup=AI)
class AIMODULE_API UAISenseImplementation_Team : public UAISenseImplementation
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	TArray<FAITeamStimulusEvent> RegisteredEvents;

public:
	FORCEINLINE static FAISenseId GetSenseIndex() { return FAISenseId(ECorePerceptionTypes::Team); }
		
	void RegisterEvent(const FAITeamStimulusEvent& Event);	

protected:
	virtual float Update() override;
};

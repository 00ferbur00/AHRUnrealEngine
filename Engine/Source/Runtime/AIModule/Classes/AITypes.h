// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AITypes.generated.h"

namespace FAISystem
{
	static const FRotator InvalidRotation = FRotator(FLT_MAX);
	static const FVector InvalidLocation = FVector(FLT_MAX);
	static const FVector InvalidDirection = FVector::ZeroVector;
	static const float InfiniteInterval = -FLT_MAX;

	FORCEINLINE bool IsValidLocation(const FVector& TestLocation)
	{
		return -InvalidLocation.X < TestLocation.X && TestLocation.X < InvalidLocation.X
			&& -InvalidLocation.Y < TestLocation.Y && TestLocation.Y < InvalidLocation.Y
			&& -InvalidLocation.Z < TestLocation.Z && TestLocation.Z < InvalidLocation.Z;
	}

	FORCEINLINE bool IsValidDirection(const FVector& TestVector)
	{
		return IsValidLocation(TestVector) == true && TestVector.IsZero() == false;
	}

	FORCEINLINE bool IsValidRotation(const FRotator& TestRotation)
	{
		return TestRotation != InvalidRotation;
	}
}

UENUM()
namespace EAIOptionFlag
{
	enum Type
	{
		Default,
		Enable UMETA(DisplayName = "Yes"),	// UHT was complaining when tried to use True as value instead of Enable
		Disable UMETA(DisplayName = "No"),

		MAX UMETA(Hidden)
	};
}

namespace EAIForceParam
{
	enum Type
	{
		Force,
		DoNotForce,

		MAX UMETA(Hidden)
	};
}

namespace FAIMoveFlag
{
	static const bool StopOnOverlap = true;
	static const bool UsePathfinding = true;
	static const bool IgnorePathfinding = false;
}

namespace EAILogicResuming
{
	enum Type
	{
		Continue,
		RestartedInstead,
	};
}

UENUM()
namespace EPawnActionAbortState
{
	enum Type
	{
		NeverStarted,
		NotBeingAborted,
		MarkPendingAbort,	// this means waiting for child to abort before aborting self
		LatentAbortInProgress,
		AbortDone,

		MAX UMETA(Hidden)
	};
}

UENUM()
namespace EPawnActionResult
{
	enum Type
	{
		NotStarted,
		InProgress,
		Success,
		Failed,
		Aborted
	};
}

UENUM()
namespace EPawnActionEventType
{
	enum Type
	{
		Invalid,
		FailedToStart,
		FinishedAborting,
		FinishedExecution,
		Push,
	};
}

UENUM()
namespace EAIRequestPriority
{
	enum Type
	{
		SoftScript, // actions requested by Level Designers by placing AI-hinting elements on the map
		Logic,	// actions AI wants to do due to its internal logic				
		HardScript, // actions LDs really want AI to perform
		Reaction,	// actions being result of game-world mechanics, like hit reactions, death, falling, etc. In general things not depending on what AI's thinking
		Ultimate,	// ultimate priority, to be used with caution, makes AI perform given action regardless of anything else (for example disabled reactions)

		MAX UMETA(Hidden)
	};
}

namespace EAIRequestPriority
{
	static const int32 Lowest = Logic;
};

UENUM()
namespace EAILockSource
{
	enum Type
	{
		Animation,
		Logic,
		Script,
		Gameplay,

		MAX UMETA(Hidden)
	};
}

struct AIMODULE_API FAIResourceID
{
	const uint8 Index;
	const FName Name;
private:
	static uint32 NextAvailableID;
public:

	FAIResourceID(const FName& ResourceName)
		: Index(NextAvailableID++), Name(ResourceName)
	{}

	FAIResourceID(const FAIResourceID& Other)
		: Index(Other.Index), Name(Other.Name)
	{}

	FAIResourceID& operator=(const FAIResourceID& Other)
	{
		new(this) FAIResourceID(Other);
		return *this;
	}

	static uint32 ResourcesCount() { return NextAvailableID; }
};

struct AIMODULE_API FAIResourcesSet
{
	static const uint32 NoResources = 0;
	static const uint32 AllResources = uint32(-1);
private:
	uint32 Flags;
public:
	FAIResourcesSet(uint32 ResourceSetDescription = NoResources) : Flags(ResourceSetDescription) {}

	FAIResourcesSet& AddResourceID(uint8 ResourceID) { Flags |= (1 << ResourceID); return *this; }
	FAIResourcesSet& RemoveResourceID(uint8 ResourceID) { Flags &= ~(1 << ResourceID); return *this; }
	bool ContainsResourceID(uint8 ResourceID) const { return (Flags & ResourceID) != 0; }

	FAIResourcesSet& AddResource(const FAIResourceID& Resource) { AddResourceID(Resource.Index); return *this; }		
	FAIResourcesSet& RemoveResource(const FAIResourceID& Resource) { RemoveResourceID(Resource.Index); return *this; }	
	bool ContainsResource(const FAIResourceID& Resource) const { return ContainsResourceID(Resource.Index); }
	
	bool IsEmpty() const { return Flags == 0; }
};

/** structure used to define which subsystem requested locking of a specific AI resource (like movement, logic, etc.) */
struct AIMODULE_API FAIResourceLock
{
	uint8 Locks[EAILockSource::MAX];

	FAIResourceLock();

	FORCEINLINE void SetLock(EAILockSource::Type LockSource)
	{
		check(LockSource != EAILockSource::MAX);
		Locks[LockSource] = 1;
	}

	FORCEINLINE void ClearLock(EAILockSource::Type LockSource)
	{
		check(LockSource != EAILockSource::MAX);
		Locks[LockSource] = 0;
	}

	/** force-clears all locks */
	void ForceClearAllLocks();

	FORCEINLINE bool IsLocked() const
	{
		for (int32 LockLevel = 0; LockLevel < int32(EAILockSource::MAX); ++LockLevel)
		{
			if (Locks[LockLevel])
			{
				return true;
			}
		}
		return false;
	}

	FORCEINLINE bool IsLocked(EAILockSource::Type LockSource) const
	{
		check(LockSource != EAILockSource::MAX);
		return Locks[LockSource] > 0;
	}

	FString GetLockSourceName() const;
};

USTRUCT()
struct AIMODULE_API FAIRequestID
{
	GENERATED_USTRUCT_BODY()
		
private:
	static const uint32 AnyRequestID = 0;
	static const uint32 InvalidRequestID = uint32(-1);

	UPROPERTY()
	uint32 RequestID;

public:
	FAIRequestID(uint32 InRequestID = InvalidRequestID) : RequestID(InRequestID)
	{}

	/** returns true if given ID is identical to stored ID or any of considered
	 *	IDs is FAIRequestID::AnyRequest*/
	FORCEINLINE bool IsEquivalent(uint32 OtherID) const 
	{
		return OtherID != InvalidRequestID && this->IsValid() && (RequestID == OtherID || RequestID == AnyRequestID || OtherID == AnyRequestID);
	}

	FORCEINLINE bool IsEquivalent(FAIRequestID Other) const
	{
		return IsEquivalent(Other.RequestID);
	}

	FORCEINLINE bool IsValid() const
	{
		return RequestID != InvalidRequestID;
	}

	FORCEINLINE uint32 GetID() const { return RequestID; }

	void operator=(uint32 OtherID)
	{
		RequestID = OtherID;
	}

	operator uint32() const
	{
		return RequestID;
	}

	static const FAIRequestID AnyRequest;
	static const FAIRequestID CurrentRequest;
	static const FAIRequestID InvalidRequest;
};

// used in BT nodes' memory for nodes spawning actions
enum class EBTActionMemoryHelper : uint8
{
	ActionAbortingDone = 0,
	WaitingForActionToFinishAborting = 1
};

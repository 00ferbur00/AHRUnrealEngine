// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "AnimationUtils.h"
#include "AnimationRuntime.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimInstance.h"

#define NOTIFY_TRIGGER_OFFSET KINDA_SMALL_NUMBER;

float GetTriggerTimeOffsetForType(EAnimEventTriggerOffsets::Type OffsetType)
{
	switch (OffsetType)
	{
	case EAnimEventTriggerOffsets::OffsetBefore:
		{
			return -NOTIFY_TRIGGER_OFFSET;
			break;
		}
	case EAnimEventTriggerOffsets::OffsetAfter:
		{
			return NOTIFY_TRIGGER_OFFSET;
			break;
		}
	case EAnimEventTriggerOffsets::NoOffset:
		{
			return 0.f;
			break;
		}
	default:
		{
			check(false); // Unknown value supplied for OffsetType
			break;
		}
	}
	return 0.f;
}

/////////////////////////////////////////////////////
// FAnimNotifyEvent

void FAnimNotifyEvent::RefreshTriggerOffset(EAnimEventTriggerOffsets::Type PredictedOffsetType)
{
	if(PredictedOffsetType == EAnimEventTriggerOffsets::NoOffset || TriggerTimeOffset == 0.f)
	{
		TriggerTimeOffset = GetTriggerTimeOffsetForType(PredictedOffsetType);
	}
}

void FAnimNotifyEvent::RefreshEndTriggerOffset( EAnimEventTriggerOffsets::Type PredictedOffsetType )
{
	if(PredictedOffsetType == EAnimEventTriggerOffsets::NoOffset || EndTriggerTimeOffset == 0.f)
	{
		EndTriggerTimeOffset = GetTriggerTimeOffsetForType(PredictedOffsetType);
	}
}

float FAnimNotifyEvent::GetTriggerTime() const
{
	return DisplayTime + TriggerTimeOffset;
}

float FAnimNotifyEvent::GetEndTriggerTime() const
{
	return GetTriggerTime() + Duration + EndTriggerTimeOffset;
}

/////////////////////////////////////////////////////
// FFloatCurve

void FFloatCurve::SetCurveTypeFlag(EAnimCurveFlags InFlag, bool bValue)
{
	if (bValue)
	{
		CurveTypeFlags |= InFlag;
	}
	else
	{
		CurveTypeFlags &= ~InFlag;
	}
}

void FFloatCurve::ToggleCurveTypeFlag(EAnimCurveFlags InFlag)
{
	bool Current = GetCurveTypeFlag(InFlag);
	SetCurveTypeFlag(InFlag, !Current);
}

bool FFloatCurve::GetCurveTypeFlag(EAnimCurveFlags InFlag) const
{
	return (CurveTypeFlags & InFlag) != 0;
}


void FFloatCurve::SetCurveTypeFlags(int32 NewCurveTypeFlags)
{
	CurveTypeFlags = NewCurveTypeFlags;
}

int32 FFloatCurve::GetCurveTypeFlags() const
{
	return CurveTypeFlags;
}

/////////////////////////////////////////////////////
// FRawCurveTracks

void FRawCurveTracks::EvaluateCurveData(class UAnimInstance* Instance, float CurrentTime, float BlendWeight ) const
{
	// evaluate the curve data at the CurrentTime and add to Instance
	for (auto CurveIter = FloatCurves.CreateConstIterator(); CurveIter; ++CurveIter)
	{
		const FFloatCurve& Curve = *CurveIter;

		Instance->AddCurveValue( Curve.CurveUid, Curve.FloatCurve.Eval(CurrentTime)*BlendWeight, Curve.GetCurveTypeFlags() );
	}
}

FFloatCurve * FRawCurveTracks::GetCurveData(USkeleton::AnimCurveUID Uid)
{
	for(FFloatCurve& Curve : FloatCurves)
	{
		if(Curve.CurveUid == Uid)
		{
			return &Curve;
		}
	}

	return NULL;
}

bool FRawCurveTracks::DeleteCurveData(USkeleton::AnimCurveUID Uid)
{
	for( int32 Idx = 0; Idx < FloatCurves.Num(); ++Idx )
	{
		if( FloatCurves[Idx].CurveUid == Uid )
		{
			FloatCurves.RemoveAt(Idx);
			return true;
		}
	}

	return false;
}

bool FRawCurveTracks::AddCurveData(USkeleton::AnimCurveUID Uid, int32 CurveFlags /*= ACF_DefaultCurve*/)
{
	if(GetCurveData(Uid) == NULL)
	{
		FloatCurves.Add(FFloatCurve(Uid, CurveFlags));
		return true;
	}
	return false;
}

void FRawCurveTracks::Serialize(FArchive& Ar)
{
	if(Ar.UE4Ver() >= VER_UE4_SKELETON_ADD_SMARTNAMES)
	{
		for(FFloatCurve& Curve : FloatCurves)
		{
			Curve.Serialize(Ar);
		}
	}
}

ENGINE_API void FRawCurveTracks::UpdateLastObservedNames(FSmartNameMapping* NameMapping)
{
	if(NameMapping)
	{
		for(FFloatCurve& Curve : FloatCurves)
		{
			NameMapping->GetName(Curve.CurveUid, Curve.LastObservedName);
		}
	}
}

ENGINE_API bool FRawCurveTracks::DuplicateCurveData(USkeleton::AnimCurveUID ToCopyUid, USkeleton::AnimCurveUID NewUid)
{
	FFloatCurve* ExistingCurve = GetCurveData(ToCopyUid);
	if(ExistingCurve && GetCurveData(NewUid) == NULL)
	{
		// Add the curve to the track and set its data to the existing curve
		FloatCurves.Add(FFloatCurve(NewUid, ExistingCurve->GetCurveTypeFlags()));
		FloatCurves.Last().FloatCurve = ExistingCurve->FloatCurve;
		
		return true;
	}
	return false;
}

/////////////////////////////////////////////////////

UAnimSequenceBase::UAnimSequenceBase(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
	, RateScale(1.0f)
{
}

void UAnimSequenceBase::PostLoad()
{
	Super::PostLoad();

	// Convert Notifies to new data
	if( GIsEditor && Notifies.Num() > 0 )
	{
		if(GetLinkerUE4Version() < VER_UE4_ANIMNOTIFY_NAMECHANGE)
		{
			LOG_SCOPE_VERBOSITY_OVERRIDE(LogAnimation, ELogVerbosity::Warning);
			// convert animnotifies
			for(int32 I = 0; I < Notifies.Num(); ++I)
			{
				if(Notifies[I].Notify != NULL)
				{
					FString Label = Notifies[I].Notify->GetClass()->GetName();
					Label = Label.Replace(TEXT("AnimNotify_"), TEXT(""), ESearchCase::CaseSensitive);
					Notifies[I].NotifyName = FName(*Label);
				}
			}
		}

		if(GetLinkerUE4Version() < VER_UE4_CLEAR_NOTIFY_TRIGGERS)
		{
			for(FAnimNotifyEvent Notify : Notifies)
			{
				if(Notify.Notify)
				{
					// Clear end triggers for notifies that are not notify states
					Notify.EndTriggerTimeOffset = 0.0f;
				}
			}
		}
	}

	if ( GetLinkerUE4Version() < VER_UE4_MORPHTARGET_CURVE_INTEGRATION )
	{
		UpgradeMorphTargetCurves();
	}
	// Ensure notifies are sorted.
	SortNotifies();

#if WITH_EDITOR
	InitializeNotifyTrack();
	UpdateAnimNotifyTrackCache();
#endif

	if(USkeleton* Skeleton = GetSkeleton())
	{
		// Get the name mapping object for curves
		FSmartNameMapping* NameMapping = Skeleton->SmartNames.GetContainer(USkeleton::AnimCurveMappingName);
		
		// Fix up the existing curves to work with smartnames
		if(GetLinkerUE4Version() < VER_UE4_SKELETON_ADD_SMARTNAMES)
		{
			for(FFloatCurve& Curve : RawCurveData.FloatCurves)
			{
				// Add the names of the curves into the smartname mapping and store off the curve uid which will be saved next time the sequence saves.
				NameMapping->AddName(Curve.LastObservedName, Curve.CurveUid);
			}
		}
		else
		{
			TArray<FFloatCurve*> UnlinkedCurves;
			for(FFloatCurve& Curve : RawCurveData.FloatCurves)
			{
				if(!NameMapping->Exists(Curve.LastObservedName))
				{
					// The skeleton doesn't know our name. Use the last observed name that was saved with the
					// curve to create a new name. This can happen if a user saves an animation but not a skeleton
					// either when updating the assets or editing the curves within.
					UnlinkedCurves.Add(&Curve);
				}
			}

			for(FFloatCurve* Curve : UnlinkedCurves)
			{
				NameMapping->AddName(Curve->LastObservedName, Curve->CurveUid);
			}
		}
	}
}

void UAnimSequenceBase::UpgradeMorphTargetCurves()
{
	// make sure this doesn't get called by anywhere else or you'll have to check this before calling 
	if ( GetLinkerUE4Version() < VER_UE4_MORPHTARGET_CURVE_INTEGRATION )
	{
		for ( auto CurveIter = RawCurveData.FloatCurves.CreateIterator(); CurveIter; ++CurveIter )
		{
			FFloatCurve & Curve = (*CurveIter);
			// make sure previous curves has editable flag
			Curve.SetCurveTypeFlag(ACF_DefaultCurve, true);
		}	
	}
}

void UAnimSequenceBase::SortNotifies()
{
	struct FCompareFAnimNotifyEvent
	{
		FORCEINLINE bool operator()( const FAnimNotifyEvent& A, const FAnimNotifyEvent& B ) const
		{
			float ATime = A.GetTriggerTime();
			float BTime = B.GetTriggerTime();

#if WITH_EDITORONLY_DATA
			// this sorting only works if it's saved in editor or loaded with editor data
			// this is required for gameplay team to have reliable order of notifies
			// but it was noted that this change will require to resave notifies. 
			// once you resave, this order will be preserved
			if(FMath::IsNearlyEqual(ATime, BTime))
			{

				// if the 2 anim notify events are the same display time sort based off of track index
				return A.TrackIndex < B.TrackIndex;
			}
			else
#endif // WITH_EDITORONLY_DATA
			{
				return ATime < BTime;
			}
		}
	};

	Notifies.Sort( FCompareFAnimNotifyEvent() );
}

/** 
 * Retrieves AnimNotifies given a StartTime and a DeltaTime.
 * Time will be advanced and support looping if bAllowLooping is true.
 * Supports playing backwards (DeltaTime<0).
 * Returns notifies between StartTime (exclusive) and StartTime+DeltaTime (inclusive)
 */
void UAnimSequenceBase::GetAnimNotifies(const float& StartTime, const float& DeltaTime, const bool bAllowLooping, TArray<const FAnimNotifyEvent *> & OutActiveNotifies) const
{
	// Early out if we have no notifies
	if( (Notifies.Num() == 0) || (DeltaTime == 0.f) )
	{
		return;
	}

	bool const bPlayingBackwards = (DeltaTime < 0.f);
	float PreviousPosition = StartTime;
	float CurrentPosition = StartTime;
	float DesiredDeltaMove = DeltaTime;

	do 
	{
		// Disable looping here. Advance to desired position, or beginning / end of animation 
		const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, SequenceLength);

		// Verify position assumptions
		check( bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition));
		
		GetAnimNotifiesFromDeltaPositions(PreviousPosition, CurrentPosition, OutActiveNotifies);
	
		// If we've hit the end of the animation, and we're allowed to loop, keep going.
		if( (AdvanceType == ETAA_Finished) &&  bAllowLooping )
		{
			const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
			DesiredDeltaMove -= ActualDeltaMove; 

			PreviousPosition = bPlayingBackwards ? SequenceLength : 0.f;
			CurrentPosition = PreviousPosition;
		}
		else
		{
			break;
		}
	} 
	while( true );
}

/** 
 * Retrieves AnimNotifies between two time positions. ]PreviousPosition, CurrentPosition]
 * Between PreviousPosition (exclusive) and CurrentPosition (inclusive).
 * Supports playing backwards (CurrentPosition<PreviousPosition).
 * Only supports contiguous range, does NOT support looping and wrapping over.
 */
void UAnimSequenceBase::GetAnimNotifiesFromDeltaPositions(const float& PreviousPosition, const float& CurrentPosition, TArray<const FAnimNotifyEvent *> & OutActiveNotifies) const
{
	// Early out if we have no notifies
	if( (Notifies.Num() == 0) || (PreviousPosition == CurrentPosition) )
	{
		return;
	}

	bool const bPlayingBackwards = (CurrentPosition < PreviousPosition);

	// If playing backwards, flip Min and Max.
	if( bPlayingBackwards )
	{
		for (int32 NotifyIndex=0; NotifyIndex<Notifies.Num(); NotifyIndex++)
		{
			const FAnimNotifyEvent& AnimNotifyEvent = Notifies[NotifyIndex];
			const float NotifyStartTime = AnimNotifyEvent.GetTriggerTime();
			const float NotifyEndTime = AnimNotifyEvent.GetEndTriggerTime();

			if( (NotifyStartTime < PreviousPosition) && (NotifyEndTime >= CurrentPosition) )
			{
				OutActiveNotifies.Add(&AnimNotifyEvent);
			}
		}
	}
	else
	{
		for (int32 NotifyIndex=0; NotifyIndex<Notifies.Num(); NotifyIndex++)
		{
			const FAnimNotifyEvent& AnimNotifyEvent = Notifies[NotifyIndex];
			const float NotifyStartTime = AnimNotifyEvent.GetTriggerTime();
			const float NotifyEndTime = AnimNotifyEvent.GetEndTriggerTime();

			if( (NotifyStartTime <= CurrentPosition) && (NotifyEndTime > PreviousPosition) )
			{
				OutActiveNotifies.Add(&AnimNotifyEvent);
			}
		}
	}
}

void UAnimSequenceBase::TickAssetPlayerInstance(const FAnimTickRecord& Instance, class UAnimInstance* InstanceOwner, FAnimAssetTickContext& Context) const
{
	float& CurrentTime = *(Instance.TimeAccumulator);
	const float PreviousTime = CurrentTime;
	const float PlayRate = Instance.PlayRateMultiplier * this->RateScale;

	float MoveDelta = 0.f;

	if( Context.IsLeader() )
	{
		const float DeltaTime = Context.GetDeltaTime();
		MoveDelta = PlayRate * DeltaTime;

		if( MoveDelta != 0.f )
		{
			// Advance time
			FAnimationRuntime::AdvanceTime(Instance.bLooping, MoveDelta, CurrentTime, SequenceLength);
		}

		Context.SetSyncPoint(CurrentTime / SequenceLength);
	}
	else
	{
		// Follow the leader
		CurrentTime = Context.GetSyncPoint() * SequenceLength;
		//@TODO: NOTIFIES: Calculate AdvanceType based on what the new delta time is

		if( CurrentTime != PreviousTime )
		{
			// Figure out delta time 
			MoveDelta = CurrentTime - PreviousTime;
			// if we went against play rate, then loop around.
			if( (MoveDelta * PlayRate) < 0.f )
			{
				MoveDelta += FMath::Sign<float>(PlayRate) * SequenceLength;
			}
		}
	}

	OnAssetPlayerTickedInternal(Context, PreviousTime, MoveDelta, Instance, InstanceOwner);

	// Evaluate Curve data now - even if time did not move, we still need to return curve if it exists
	EvaluateCurveData(InstanceOwner, CurrentTime, Instance.EffectiveBlendWeight);
}

#if WITH_EDITOR

void UAnimSequenceBase::UpdateAnimNotifyTrackCache()
{
	SortNotifies();

	for (int32 TrackIndex=0; TrackIndex<AnimNotifyTracks.Num(); ++TrackIndex)
	{
		AnimNotifyTracks[TrackIndex].Notifies.Empty();
	}

	for (int32 NotifyIndex = 0; NotifyIndex<Notifies.Num(); ++NotifyIndex)
	{
		int32 TrackIndex = Notifies[NotifyIndex].TrackIndex;
		if (AnimNotifyTracks.IsValidIndex(TrackIndex))
		{
			AnimNotifyTracks[TrackIndex].Notifies.Add(&Notifies[NotifyIndex]);
		}
		else
		{
			// this notifyindex isn't valid, delete
			// this should not happen, but if it doesn, find best place to add
			ensureMsg(0, TEXT("AnimNotifyTrack: Wrong indices found"));
			AnimNotifyTracks[0].Notifies.Add(&Notifies[NotifyIndex]);
		}
	}

	// notification broadcast
	OnNotifyChanged.Broadcast();
}

void UAnimSequenceBase::InitializeNotifyTrack()
{
	if ( AnimNotifyTracks.Num() == 0 ) 
	{
		AnimNotifyTracks.Add(FAnimNotifyTrack(TEXT("1"), FLinearColor::White ));
	}
}

int32 UAnimSequenceBase::GetNumberOfFrames()
{
	return (SequenceLength/0.033f);
}

void UAnimSequenceBase::RegisterOnNotifyChanged(const FOnNotifyChanged& Delegate)
{
	OnNotifyChanged.Add(Delegate);
}
void UAnimSequenceBase::UnregisterOnNotifyChanged(void* Unregister)
{
	OnNotifyChanged.RemoveAll(Unregister);
}

void UAnimSequenceBase::ClampNotifiesAtEndOfSequence()
{
	const float NotifyClampTime = SequenceLength - 0.01f; //Slight offset so that notify is still draggable
	for(int i = 0; i < Notifies.Num(); ++ i)
	{
		if(Notifies[i].DisplayTime >= SequenceLength)
		{
			Notifies[i].DisplayTime = NotifyClampTime;
			Notifies[i].TriggerTimeOffset = GetTriggerTimeOffsetForType(EAnimEventTriggerOffsets::OffsetBefore);
		}
	}
}

EAnimEventTriggerOffsets::Type UAnimSequenceBase::CalculateOffsetForNotify(float NotifyDisplayTime) const
{
	if(NotifyDisplayTime == 0.f)
	{
		return EAnimEventTriggerOffsets::OffsetAfter;
	}
	else if(NotifyDisplayTime == SequenceLength)
	{
		return EAnimEventTriggerOffsets::OffsetBefore;
	}
	return EAnimEventTriggerOffsets::NoOffset;
}

void UAnimSequenceBase::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);
	if(Notifies.Num() > 0)
	{
		FString NotifyList;

		// add notifies to 
		for(auto Iter=Notifies.CreateConstIterator(); Iter; ++Iter)
		{
			// only add if not BP anim notify since they're handled separate
			if(Iter->IsBlueprintNotify() == false)
			{
				NotifyList += FString::Printf(TEXT("%s%c"), *Iter->NotifyName.ToString(), USkeleton::AnimNotifyTagDelimiter);
			}
		}
		
		if(NotifyList.Len() > 0)
		{
			OutTags.Add(FAssetRegistryTag(USkeleton::AnimNotifyTag, NotifyList, FAssetRegistryTag::TT_Hidden));
		}
	}

	// Add curve IDs to a tag list, or a blank tag if we have no curves.
	// The blank list is necessary when we attempt to delete a curve so
	// an old asset can be detected from its asset data so we load as few
	// as possible.
	FString CurveIdList;

	for(const FFloatCurve& Curve : RawCurveData.FloatCurves)
	{
		CurveIdList += FString::Printf(TEXT("%u%c"), Curve.CurveUid, USkeleton::CurveTagDelimiter);
	}
	OutTags.Add(FAssetRegistryTag(USkeleton::CurveTag, CurveIdList, FAssetRegistryTag::TT_Hidden));
}

uint8* UAnimSequenceBase::FindNotifyPropertyData(int32 NotifyIndex, UArrayProperty*& ArrayProperty)
{
	// initialize to NULL
	ArrayProperty = NULL;

	if(Notifies.IsValidIndex(NotifyIndex))
	{
		// find Notifies property start point
		UProperty* Property = FindField<UProperty>(GetClass(), TEXT("Notifies"));

		// found it and if it is array
		if(Property && Property->IsA(UArrayProperty::StaticClass()))
		{
			// find Property Value from UObject we got
			uint8* PropertyValue = Property->ContainerPtrToValuePtr<uint8>(this);

			// it is array, so now get ArrayHelper and find the raw ptr of the data
			ArrayProperty = CastChecked<UArrayProperty>(Property);
			FScriptArrayHelper ArrayHelper(ArrayProperty, PropertyValue);

			if(ArrayProperty->Inner && NotifyIndex < ArrayHelper.Num())
			{
				//Get property data based on selected index
				return ArrayHelper.GetRawPtr(NotifyIndex);
			}
		}
	}
	return NULL;
}

#endif	//WITH_EDITOR


/** Add curve data to Instance at the time of CurrentTime **/
void UAnimSequenceBase::EvaluateCurveData(class UAnimInstance* Instance, float CurrentTime, float BlendWeight ) const
{
	// if we have compression, this should change
	// this is raw data evaluation
	RawCurveData.EvaluateCurveData(Instance, CurrentTime, BlendWeight);
}

void UAnimSequenceBase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if(Ar.ArIsSaving && Ar.UE4Ver() >= VER_UE4_SKELETON_ADD_SMARTNAMES)
	{
		if(USkeleton* Skeleton = GetSkeleton())
		{
			FSmartNameMapping* Mapping = GetSkeleton()->SmartNames.GetContainer(USkeleton::AnimCurveMappingName);
			check(Mapping); // Should always exist
			RawCurveData.UpdateLastObservedNames(Mapping);
		}
	}
	RawCurveData.Serialize(Ar);
}

void UAnimSequenceBase::OnAssetPlayerTickedInternal(FAnimAssetTickContext &Context, const float PreviousTime, const float MoveDelta, const FAnimTickRecord &Instance, class UAnimInstance* InstanceOwner) const
{
	if (Context.ShouldGenerateNotifies())
	{
		// Harvest and record notifies
		TArray<const FAnimNotifyEvent*> AnimNotifies;
		GetAnimNotifies(PreviousTime, MoveDelta, Instance.bLooping, AnimNotifies);
		InstanceOwner->AddAnimNotifies(AnimNotifies, Instance.EffectiveBlendWeight);
	}
}

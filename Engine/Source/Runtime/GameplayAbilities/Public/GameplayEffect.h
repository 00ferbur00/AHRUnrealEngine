// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagAssetInterface.h"
#include "AbilitySystemLog.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffect.generated.h"

struct FActiveGameplayEffect;

class UGameplayEffect;
class UGameplayEffectTemplate;
class UAbilitySystemComponent;



USTRUCT()
struct FGameplayModifierCallbacks
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditDefaultsOnly, Category = GameplayModifier)
	TArray<TSubclassOf<class UGameplayEffectExtension> >	ExtensionClasses;
};

USTRUCT()
struct FGameplayEffectStackingCallbacks
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditDefaultsOnly, Category = GEStack)
	TArray<TSubclassOf<class UGameplayEffectStackingExtension> >	ExtensionClasses;
};

/**
* Defines how A GameplayEffect levels
*	Normally, GameplayEffect levels are specified when they are created.
*	They can also be tied to their instigators attribute.
*		For example, a Damage apply GameplayEffect that 'levels' based on the PhysicalDamage attribute
*/
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayEffectLevelDef
{
	GENERATED_USTRUCT_BODY()

	/** When true, whatever creates or owns this will pass in a level. E.g, level is not intrinsic to this definition. */
	UPROPERTY(EditDefaultsOnly, Category = GameplayEffectLevel)
	bool InheritLevelFromOwner;

	/** If set, the gameplay effect's level will be tied to this attribute on the instigator */
	UPROPERTY(EditDefaultsOnly, Category = GameplayEffectLevel, meta = (FilterMetaTag="HideFromLevelInfos"))
	FGameplayAttribute	Attribute;

	/** If true, take snapshot of attribute level when the gameplay effect is initialized. Otherwise, the level of the gameplay effect will update as the attribute it is tied to updates */
	UPROPERTY(EditDefaultsOnly, Category = GameplayEffectLevel)
	bool TakeSnapshotOnInit;
};

/**
 * FGameplayModifierInfo
 *	Tells us "Who/What we" modify
 *	Does not tell us how exactly
 *
 */
USTRUCT()
struct GAMEPLAYABILITIES_API FGameplayModifierInfo
{
	GENERATED_USTRUCT_BODY()

	FGameplayModifierInfo()
		: ModifierType( EGameplayMod::Attribute )
		, ModifierOp( EGameplayModOp::Additive )
		, EffectType( EGameplayModEffect::Magnitude )
		, TargetEffect( NULL )
	{

	}

	/** How much this modifies what it is applied to */
	UPROPERTY(EditDefaultsOnly, Category = GameplayModifier)
	FScalableFloat Magnitude; // Not modified from defaults

	/** What this modifies - Attribute, OutgoingGEs, IncomingGEs, ACtiveGEs. */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	TEnumAsByte<EGameplayMod::Type> ModifierType;

	/** The Attribute we modify or the GE we modify modifies. */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	FGameplayAttribute Attribute;

	/** The numeric operation of this modifier: Override, Add, Multiply, etc  */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	TEnumAsByte<EGameplayModOp::Type> ModifierOp;

	/** If we modify an effect, this is what we modify about it (Duration, Magnitude, etc) */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	TEnumAsByte<EGameplayModEffect::Type> EffectType;

	/** If we are linking a gameplay effect to another effect, this is the effect to link */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	UGameplayEffect* TargetEffect;

	/** The thing I modify requires these tags */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	FGameplayTagContainer RequiredTags;

	/** The thing I modify must not have any of these tags */
	UPROPERTY(EditDefaultsOnly, Category = GameplayModifier)
	FGameplayTagContainer IgnoreTags;
	
	/** This modifier's tags. These tags are passed to any other modifiers that this modifies. */
	UPROPERTY(EditDefaultsOnly, Category=GameplayModifier)
	FGameplayTagContainer OwnedTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = GameplayEffect)
	FGameplayEffectLevelDef	LevelInfo;

	FString ToSimpleString() const
	{
		return FString::Printf(TEXT("%s %s %s BaseVaue: %s"), *EGameplayModToString(ModifierType), *EGameplayModOpToString(ModifierOp), *EGameplayModEffectToString(EffectType), *Magnitude.ToSimpleString());
	}

	UPROPERTY(EditDefaultsOnly, Category = GameplayModifier)
	FGameplayModifierCallbacks	Callbacks;
};

/**
 * FGameplayEffectCue
 *	This is a cosmetic cue that can be tied to a UGameplayEffect. 
 *  This is essentially a GameplayTag + a Min/Max level range that is used to map the level of a GameplayEffect to a normalized value used by the GameplayCue system.
 */
USTRUCT()
struct FGameplayEffectCue
{
	GENERATED_USTRUCT_BODY()

	FGameplayEffectCue()
		: MinLevel(0.f)
		, MaxLevel(0.f)
	{
	}

	FGameplayEffectCue(const FGameplayTag& InTag, float InMinLevel, float InMaxLevel)
		: MinLevel(InMinLevel)
		, MaxLevel(InMaxLevel)
	{
		GameplayCueTags.AddTag(InTag);
	}

	/** The minimum level that this Cue supports */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue)
	float	MinLevel;

	/** The maximum level that this Cue supports */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue)
	float	MaxLevel;

	/** Tags passed to the gameplay cue handler when this cue is activated */
	UPROPERTY(EditDefaultsOnly, Category = GameplayCue, meta = (Categories="GameplayCue"))
	FGameplayTagContainer GameplayCueTags;

	float NormalizeLevel(float InLevel)
	{
		float Range = MaxLevel - MinLevel;
		if (Range <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}

		return FMath::Clamp((InLevel - MinLevel) / Range, 0.f, 1.0f);
	}
};

/**
 * UGameplayEffect
 *	The GameplayEffect definition. This is the data asset defined in the editor that drives everything.
 */
UCLASS(BlueprintType)
class GAMEPLAYABILITIES_API UGameplayEffect : public UDataAsset, public IGameplayTagAssetInterface
{

public:
	GENERATED_UCLASS_BODY()

	/** Infinite duration */
	static const float INFINITE_DURATION;

	/** No duration; Time specifying instant application of an effect */
	static const float INSTANT_APPLICATION;

	/** Constant specifying that the combat effect has no period and doesn't check for over time application */
	static const float NO_PERIOD;

#if WITH_EDITORONLY_DATA
	/** Template to derive starting values and editing customization from */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Template)
	UGameplayEffectTemplate*	Template;

	/** When false, show a limited set of properties for editing, based on the template we are derived from */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Template)
	bool ShowAllProperties;
#endif

	/** Duration in seconds. 0.0 for instantaneous effects; -1.0 for infinite duration. */
	UPROPERTY(EditDefaultsOnly, Category=GameplayEffect)
	FScalableFloat	Duration;

	/** Period in seconds. 0.0 for non-periodic effects */
	UPROPERTY(EditDefaultsOnly, Category=GameplayEffect)
	FScalableFloat	Period;
	
	/** Array of modifiers that will affect the target of this effect */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=GameplayEffect)
	TArray<FGameplayModifierInfo> Modifiers;

	/** Array of level definitions that will determine how this GameplayEffect scales */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category=GameplayEffect)
	FGameplayEffectLevelDef	LevelInfo;
		
	// "I can only be applied to targets that have these tags"
	// "I can only exist on CE buckets on targets that have these tags":
	
	/** Container of gameplay tags that have to be present on the target actor for the effect to be applied */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Application)
	FGameplayTagContainer ApplicationRequiredTargetTags;

	// "I can only be applied if my instigator has these tags"

	/** Container of gameplay tags that have to be present on the instigator actor for the effect to be applied */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Application)
	FGameplayTagContainer ApplicationRequiredInstigatorTags;

	/** Probability that this gameplay effect will be applied to the target actor (0.0 for never, 1.0 for always) */
	UPROPERTY(EditDefaultsOnly, Category=Application, meta=(GameplayAttribute="True"))
	FScalableFloat	ChanceToApplyToTarget;

	/** Probability that this gameplay effect will execute on another GE after it has been successfully applied to the target actor (0.0 for never, 1.0 for always) */
	UPROPERTY(EditDefaultsOnly, Category = Application, meta = (GameplayAttribute = "True"))
	FScalableFloat	ChanceToExecuteOnGameplayEffect;

	/** other gameplay effects that will be applied to the target of this effect if this effect applies */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = GameplayEffect)
	TArray<UGameplayEffect*> TargetEffects;

	/** removes active gameplay effects and stops gameplay effects from applying if the tags and qualification context match */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = GameplayEffect)
	TEnumAsByte<EGameplayImmunity::Type> AppliesImmunityTo;

	// Modify duration of CEs

	// Modify MaxStacks of CEs (maybe... probably not)

	// ------------------------------------------------
	// Functions
	// ------------------------------------------------

	/**
	 * Determines if the set of supplied gameplay tags are enough to satisfy the application tag requirements of the effect
	 * 
	 * @param InstigatorTags	Owned gameplay tags of the instigator applying the effect
	 * @param TargetTags		Owned gameplay tags of the target about to be affected by the effect
	 * @return					True if the instigator and target actor tags meet the requirements for this gameplay effect, false otherwise
	 */
	bool AreApplicationTagRequirementsSatisfied(const FGameplayTagContainer& InstigatorTags, const FGameplayTagContainer& TargetTags) const;

	// ------------------------------------------------
	// New Tagging functionality
	// ------------------------------------------------

	/** "These are my tags" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Tags)
	FGameplayTagContainer GameplayEffectTags;

	/** "In order to affect another GameplayEffect, they must have ALL of these tags" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Tags)
	FGameplayTagContainer GameplayEffectRequiredTags;
	
	/** "In order to affect another GameplayEffect, they must NOT have ANY of these tags" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Tags)
	FGameplayTagContainer GameplayEffectIgnoreTags;

	/**
	* Can this GameplayEffect modify a GameplayEffect that owns Tags
	*
	* @param Tags	Owned gameplay tags of the gameplay effect this effect is being applied to
	* @return		True if the tags meet the requirements for this gameplay effect, false otherwise
	*/
	bool AreGameplayEffectTagRequirementsSatisfied(const FGameplayTagContainer& Tags) const
	{
		bool bHasRequired = Tags.MatchesAll(GameplayEffectRequiredTags, true);
		bool bHasIgnored = Tags.MatchesAny(GameplayEffectIgnoreTags, false);

		return bHasRequired && !bHasIgnored;
	}

	/**
	* Can this GameplayEffect modify the input parameter, based on tags
	*
	* @param GameplayEffectToBeModified	A GameplayEffect we are trying to apply this GameplayEffect to.
	* @return							True if the tags owned by GameplayEffectToBeModified meet the requirements for this gameplay effect, false otherwise
	*/
	bool AreGameplayEffectTagRequirementsSatisfied(const UGameplayEffect *GameplayEffectToBeModified) const
	{
		return AreGameplayEffectTagRequirementsSatisfied(GameplayEffectToBeModified->GameplayEffectTags);
	}

	// ------------------------------------------------
	// Gameplay tag interface
	// ------------------------------------------------

	/** Overridden to return requirements tags */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	/** Get the "clear tags" for the effect */
	virtual void GetClearGameplayTags(FGameplayTagContainer& TagContainer) const;

	void ValidateGameplayEffect();

	/** "These tags are applied to the actor I am applied to" */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Tags)
	FGameplayTagContainer OwnedTagsContainer;

	/** Container of gameplay tags to be cleared upon effect application; Any active effects with these tags that can be cleared, will be */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category=Tags)
	FGameplayTagContainer ClearTagsContainer;
	
	// Used to quickly tell if a GameplayEffect modifies another GameplayEffect (or a set of attributes)
	bool ModifiesAnyProperties(EGameplayMod::Type ModType, const TSet<UProperty> & Properties)
	{
		return false;
	}

	virtual void PostLoad() override;

	// -----------------------------------------------
	
	/** Should copies of this GameplayEffect be a snapshot of the current state or update when it does (linked) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Advanced)
	TEnumAsByte<EGameplayEffectCopyPolicy::Type>	CopyPolicy;

	// ----------------------------------------------

	/** Cues to trigger non-simulated reactions in response to this GameplayEffect such as sounds, particle effects, etc */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Display)
	TArray<FGameplayEffectCue>	GameplayCues;

	/** Description of this gameplay effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = Display)
	FText Description;

	// ----------------------------------------------

	/** Specifies the rule used to stack this GameplayEffect with other GameplayEffects. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	TEnumAsByte<EGameplayEffectStackingPolicy::Type>	StackingPolicy;

	/** An identifier for the stack. Both names and stacking policy must match for GameplayEffects to stack with each other. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = Stacking)
	FName StackedAttribName;

	/** Specifies a custom stacking rule if one is needed. */
	UPROPERTY(EditDefaultsOnly, Category = Stacking)
	TSubclassOf<class UGameplayEffectStackingExtension> StackingExtension;


	/** Hack! Fixes issues in PIE when you create a dataasset and it cant be used in networking until you reload the editor */
	bool IsNameStableForNetworking() const override
	{
		return true;
	}

protected:
	void ValidateStacking();


};

/**
 * FGameplayEffectLevelSpec
 *	Level Specification. This can be a static, constant level specified on creation or it can be dynamically tied to a source's attribute value.
 *	For example, a GameplayEffect could be made whose level is tied to its instigators PhysicalDamage or Intelligence attribute.
 */

struct FGameplayEffectLevelSpec
{
	static const float INVALID_LEVEL;

	FGameplayEffectLevelSpec()
		:ConstantLevel(INVALID_LEVEL)
		,CachedLevel(INVALID_LEVEL)
	{
	}

	FGameplayEffectLevelSpec(float InLevel, const FGameplayEffectLevelDef &Def, class AActor *InSource)
		: ConstantLevel(InLevel)
		, CachedLevel(InLevel)
		, Source(InSource)
	{
		if (Def.Attribute.GetUProperty() != NULL)
		{
			Attribute = Def.Attribute;
		}

		if (Def.TakeSnapshotOnInit)
		{
			SnapshotLevel();
		}
	}

	void ApplyNewDef(const FGameplayEffectLevelDef &Def, TSharedPtr<FGameplayEffectLevelSpec> &OutSharedPtr) const
	{
		if (Def.InheritLevelFromOwner)
		{
			return;
		}

		check(OutSharedPtr.IsValid());
		if (Def.Attribute != OutSharedPtr->Attribute)
		{
			// In def levels off something different
			// make a new level spec
			OutSharedPtr = TSharedPtr<FGameplayEffectLevelSpec>(new FGameplayEffectLevelSpec(INVALID_LEVEL, Def, Source.Get()));
		}
	}

	/** Dynamic simply means the level may change. It is not constant. */
	bool IsDynamic() const
	{
		return ConstantLevel == INVALID_LEVEL && Attribute.GetUProperty() != NULL;
	}

	/** Valid means we have some meaningful data. If we have an INVALID_LEVEL constant value and are not tied to a dynamic property, then we are invalid. */
	bool IsValid() const
	{
		return ConstantLevel != INVALID_LEVEL || Attribute.GetUProperty() != NULL;
	}

	float GetLevel() const;

	void SnapshotLevel()
	{
		// This should snapshot the current level (if dynamic/delegate) and save off its value so that it doesn't change
		ConstantLevel = GetLevel();
		Source = NULL;
	}

	void RegisterLevelDependancy(TWeakPtr<struct FAggregator> OwningAggregator);

	void PrintAll() const;

	mutable float ConstantLevel;	// Final/constant level. Once this is set we are locked at the given level.
	mutable float CachedLevel;		// Last read value. Needed in case we lose our source, we use the last known level.

	TWeakObjectPtr<class AActor>	Source;

	FGameplayAttribute	Attribute;
};


/**
 * FAggregatorRef
 *	A reference to an FAggregator. The reference may be weak or hard, and this can be changed over the lifetime of the FAggregatorRef.
 *	
 *	There are cases where we want weak references in an aggregator chain. 
 *		For example a RunSpeed buff, which when it is destroyed we want the RunSpeed attribute aggregator to recalculate the RunSpeed value.
 *
 *	There are cases where we want to make a copy of what we are weak referencing and make the reference a hard ref to that copy
 *		For example, a DOT which is buffed is attached to a target. We want to make a copy of the DOT and its buff then give it to the target as a hard ref so that if 
 *		the buff expires on the source, the applied DOT is still buffed.
 *
 */
USTRUCT()
struct FAggregatorRef
{
	GENERATED_USTRUCT_BODY()

	friend struct FAggregatorRef;

	FAggregatorRef()
	{
	}

	FAggregatorRef(struct FAggregator *Src)
		: SharedPtr(Src)
	{		 
		WeakPtr = SharedPtr;
	}

	FAggregatorRef(const FAggregatorRef *Src)
	{
		SetSoftRef(Src);
	}

	void MakeHardRef()
	{
		check(WeakPtr.IsValid());
		SharedPtr = WeakPtr.Pin();
	}
	void MakeSoftRef()
	{
		check(WeakPtr.IsValid());
		SharedPtr.Reset();
	}

	void SetSoftRef(const FAggregatorRef *Src)
	{
		check(!SharedPtr.IsValid());
		WeakPtr = Src->SharedPtr;
	}
	
	FAggregator * Get()
	{
		return WeakPtr.IsValid() ? WeakPtr.Pin().Get() : NULL;
	}

	const FAggregator * Get() const
	{
		return WeakPtr.IsValid() ? WeakPtr.Pin().Get() : NULL;
	}

	bool IsValid() const
	{
		return WeakPtr.IsValid();
	}

	/** Become a hard reference to a new copy of what we are reference  */
	void MakeUnique();

	/** Become a hard reference to a new copy of what we are reference AND make new copies/hard refs of the complete modifier chain in our FAggregator */
	void MakeUniqueDeep();

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);	

	FString ToString() const;
	void PrintAll() const;

private:

	TSharedPtr<struct FAggregator>	SharedPtr;
	TWeakPtr<struct FAggregator> WeakPtr;
};

template<>
struct TStructOpsTypeTraits< FAggregatorRef > : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = false
	};
};

/**
 * GameplayModifierData
 *	This is the data that FAggregator aggregates and turns into FGameplayModifierEvaluatedData.
 *  It is distinct from FGameplayModifierEvaluatedData in that FGameplayModifierData ia level has not been applied to this data.
 *  FGameplayModifierData::Magnitude is an FScalableFloat which describes a numeric value for a given level.
 * 
 */
struct FGameplayModifierData
{
	FGameplayModifierData()
	{
		
	}

	FGameplayModifierData(const FGameplayModifierInfo &Info, const FGlobalCurveDataOverride *CurveData)
	{
		Magnitude = Info.Magnitude.MakeFinalizedCopy(CurveData);
		Tags = Info.OwnedTags;

		// Fixme: this is static data, should be a reference
		RequireTags = Info.RequiredTags;
		IgnoreTags = Info.IgnoreTags;

		if (Info.Callbacks.ExtensionClasses.Num() > 0)
		{
			Callbacks = &Info.Callbacks;
		}
	}

	FGameplayModifierData(FScalableFloat InMagnitude)
	{
		// Magnitude may scale based on our level
		Magnitude = InMagnitude;
		Callbacks = NULL;
	}

	FGameplayModifierData(float InMagnitude, const FGameplayModifierCallbacks * InCallbacks)
	{
		// Magnitude will be fixed at this value
		Magnitude.SetValue(InMagnitude);
		Callbacks = InCallbacks;
	}

	// That magnitude that we modify by
	FScalableFloat Magnitude;

	// The tags I have
	FGameplayTagContainer Tags;
	
	FGameplayTagContainer RequireTags;
	FGameplayTagContainer IgnoreTags;

	// Callback information for custom logic pre/post evaluation
	const FGameplayModifierCallbacks * Callbacks;

	void PrintAll() const;
};

/**
 * GameplayModifierEvaluatedData
 *	This is the output from an FAggregator: a numeric value and a set of GameplayTags.
 */
struct FGameplayModifierEvaluatedData
{
	FGameplayModifierEvaluatedData()
		: Magnitude(0.f)
		, Callbacks(NULL)
		, IsValid(false)
	{
	}

	FGameplayModifierEvaluatedData(float InMagnitude, const FGameplayModifierCallbacks * InCallbacks = NULL, FActiveGameplayEffectHandle InHandle = FActiveGameplayEffectHandle(), const FGameplayTagContainer *InTags = NULL)
		: Magnitude(InMagnitude)
		, Callbacks(InCallbacks)
		, Handle(InHandle)
		, IsValid(true)
	{
		if (InTags)
		{
			Tags = *InTags;
		}
	}

	float	Magnitude;
	FGameplayTagContainer Tags;
	const FGameplayModifierCallbacks * Callbacks;
	FActiveGameplayEffectHandle	Handle;	// Handle of the active gameplay effect that originated us. Will be invalid in many cases
	bool IsValid;

	// Helper function for building up final values during an aggregation
	void Aggregate(OUT FGameplayTagContainer &OutTags, OUT float &OutMagnitude, const float Bias=0.f) const;

	void InvokePreExecute(struct FGameplayEffectModCallbackData &Data) const;
	void InvokePostExecute(const struct FGameplayEffectModCallbackData &Data) const;

	void PrintAll() const;
};

/**
 * FAggregator - a data structure for aggregating stuff in GameplayEffects.
 *	Aggregates a numeric value (float) and a set of gameplay tags. This could be further extended.
 *
 *	Aggregation is done with BaseData + Mods[].
 *	-BaseData is simply the base data. We are initiliazed with base data and base data can be directly modified via ::ExecuteMod.
 *	-Mods[] are lists of other FAggregators. That is, we have a list for each EGameplayModOp: Add, multiply, override.
 *	-These lists contain FAggregatorRefs, which may be soft or hard refs to other FAggregators.
 *	-::Evalate() takes our BaseData, and then crawls through ours Mods[] list and aggregates a final output (FGameplayModifierEvaluatedData)
 *	-Results of ::Evaluate are cached in CachedData.
 *	-FAggregator also keeps a list of weak ptrs to other FAggregators that are dependant on us. If we change, we let these aggregators know, so they can invalidate their cached data.
 *
 *
 */
struct FAggregator : public TSharedFromThis<FAggregator>
{
	DECLARE_DELEGATE_OneParam(FOnDirty, FAggregator*);

	FAggregator();
	FAggregator(const FGameplayModifierData &InBaseData, TSharedPtr<FGameplayEffectLevelSpec> LevelInfo, const TCHAR* InDebugString);
	FAggregator(const FScalableFloat &InBaseMagnitude, TSharedPtr<FGameplayEffectLevelSpec> LevelInfo, const TCHAR* InDebugString);
	FAggregator(const FGameplayModifierEvaluatedData &InEvalData, const TCHAR* InDebugString);
	FAggregator(const FAggregator &In);
	virtual ~FAggregator();

	FAggregator & MarkDirty();
	void ClearAllDependancies();

	const FGameplayModifierEvaluatedData& Evaluate() const;

	void PreEvaluate(struct FGameplayEffectModCallbackData &Data) const;
	void PostEvaluate(const struct FGameplayEffectModCallbackData &Data) const;

	void TakeSnapshotOfLevel();
	void ApplyMod(EGameplayModOp::Type ModType, FAggregatorRef Ref, bool TakeSnapshot);
	
	void ExecuteModAggr(EGameplayModOp::Type ModType, FAggregatorRef Ref);
	void ExecuteMod(EGameplayModOp::Type ModType, const FGameplayModifierEvaluatedData& EvaluatedData);

	void AddDependantAggregator(TWeakPtr<FAggregator> InDependant);

	void RegisterLevelDependancies();

	TSharedPtr<FGameplayEffectLevelSpec> Level;
	FActiveGameplayEffectHandle	ActiveHandle;	// Handle to owning active effect. Will be null in many cases.

	FGameplayModifierData		BaseData;
	TArray<FAggregatorRef>		Mods[EGameplayModOp::Max];

	TArray<TWeakPtr<FAggregator > >	Dependants;

	FOnDirty	OnDirty;

	void PrintAll() const;
	void RefreshDependencies();
	void MakeUniqueDeep();

	void SetFromNetSerialize(float NetSerialize);

	// ----------------------------------------------------------------------------
	// This is data only used in debugging/tracking where aggregator's came from
	// ----------------------------------------------------------------------------
#if SKILL_SYSTEM_AGGREGATOR_DEBUG
	FString DebugString;
	mutable int32 CopiesMade;

	FString ToSimpleString() const
	{
		return FString::Printf(TEXT("0x%X %s. CacheValid: %d Mods: [%d/%d/%d]"), this, *DebugString, CachedData.IsValid, 
			GetNumValidMods(EGameplayModOp::Override), GetNumValidMods(EGameplayModOp::Additive), GetNumValidMods(EGameplayModOp::Multiplicitive) );
	}

	struct FAllocationStats
	{
		FAllocationStats()
		{
			Reset();
		}

		void Reset()
		{
			FMemory::Memzero(this, sizeof(FAllocationStats));
		}

		int32 DefaultCStor;
		int32 ModifierCStor;
		int32 ScalableFloatCstor;
		int32 FloatCstor;
		int32 CopyCstor;

		int32 DependantsUpdated;
	};

	static FAllocationStats AllocationStats;
#else

	FString ToSimpleString() const
	{
		return FString::Printf(TEXT("CacheValid: %d Mods: [%d/%d/%d]"), CachedData.IsValid, 
			GetNumValidMods(EGameplayModOp::Override), GetNumValidMods(EGameplayModOp::Additive), GetNumValidMods(EGameplayModOp::Multiplicitive) );
	}

#endif
	

private:

	int32 GetNumValidMods(EGameplayModOp::Type Type) const
	{
		int32 Num=0;
		for (const FAggregatorRef &Agg : Mods[Type])
		{
			if (Agg.IsValid())
			{
				Num++;
			}
		}
		return Num;
	}

	mutable FGameplayModifierEvaluatedData	CachedData;
};

/**
 * Modifier Specification
 *	-Const data (FGameplayModifierInfo) tells us what we modify, what we can modify
 *	-Mutable Aggregated data tells us how we modify (magnitude).
 *  
 * Modifiers can be modified. A modifier spec holds these modifications along with a reference to the const data about the modifier.
 * 
 */
struct FModifierSpec
{
	FModifierSpec(const FGameplayModifierInfo &InInfo, TSharedPtr<FGameplayEffectLevelSpec> InLevel, const FGlobalCurveDataOverride *CurveData, AActor *Owner = NULL, float Level = 0.f);

	// Hard Ref to what we modify, this stuff is const and never changes
	const FGameplayModifierInfo &Info;
	
	FAggregatorRef Aggregator;

	TSharedPtr<FGameplayEffectSpec> TargetEffectSpec;

	// returns true if this GameplayEffectSpec can modify things in QualifierContext, returns false otherwise.
	bool CanModifyInContext(const FModifierQualifier &QualifierContext) const;
	// returns true if this GameplayEffect can modify Other, false otherwise
	bool CanModifyModifier(FModifierSpec &Other, const FModifierQualifier &QualifierContext) const;
	
	void ApplyModTo(FModifierSpec &Other, bool TakeSnapshot) const;
	void ExecuteModOn(FModifierSpec &Other) const;

	FString ToSimpleString() const
	{
		return Info.ToSimpleString();
	}

	// Can this GameplayEffect modify the input parameter, based on tags
	// Returns true if it can modify the input parameter, false otherwise
	bool AreTagRequirementsSatisfied(const FModifierSpec &ModifierToBeModified) const;

	void PrintAll() const;
};

/**
 * GameplayEffect Specification. Tells us:
 *	-What UGameplayEffect (const data)
 *	-What Level
 *  -Who instigated
 *  
 * FGameplayEffectSpec is modifiable. We start with initial conditions and modifications be applied to it. In this sense, it is stateful/mutable but it
 * is still distinct from an FActiveGameplayEffect which in an applied instance of an FGameplayEffectSpec.
 */
USTRUCT()
struct FGameplayEffectSpec
{
	GENERATED_USTRUCT_BODY()

	FGameplayEffectSpec()
		: ModifierLevel( TSharedPtr< FGameplayEffectLevelSpec >( new FGameplayEffectLevelSpec() ) )
		, Duration(new FAggregator(FGameplayModifierEvaluatedData(0.f, NULL, FActiveGameplayEffectHandle()), SKILL_AGG_DEBUG(TEXT("Uninitialized Duration"))))
		, Period(new FAggregator(FGameplayModifierEvaluatedData(0.f, NULL, FActiveGameplayEffectHandle()), SKILL_AGG_DEBUG(TEXT("Uninitialized Period"))))
		, bTopOfStack(false)
	{
		// If we initialize a GameplayEffectSpec with no level object passed in.
	}

	FGameplayEffectSpec( const UGameplayEffect *InDef, AActor *Instigator, float Level, const FGlobalCurveDataOverride *CurveData );
	
	UPROPERTY()
	const UGameplayEffect * Def;

	// Replicated	
	TSharedPtr< FGameplayEffectLevelSpec > ModifierLevel;
	
	// Replicated
	UPROPERTY()
	FGameplayEffectInstigatorContext InstigatorContext; // This tells us how we got here (who / what applied us)

	float GetDuration() const;
	float GetPeriod() const;
	float GetChanceToApplyToTarget() const;
	float GetChanceToExecuteOnGameplayEffect() const;
	float GetMagnitude(const FGameplayAttribute &Attribute) const;

	EGameplayEffectStackingPolicy::Type GetStackingType() const;

	// other effects that need to be applied to the target if this effect is successful
	TArray< TSharedRef< FGameplayEffectSpec > > TargetEffectSpecs;

	// The duration in seconds of this effect
	// instantaneous effects should have a duration of UGameplayEffect::INSTANT_APPLICATION
	// effects that last forever should have a duration of UGameplayEffect::INFINITE_DURATION
	UPROPERTY()
	FAggregatorRef	Duration;

	// The period in seconds of this effect.
	// Nonperiodic effects should have a period of UGameplayEffect::NO_PERIOD
	UPROPERTY()
	FAggregatorRef	Period;

	// The chance, in a 0.0-1.0 range, that this GameplayEffect will be applied to the target Attribute or GameplayEffect.
	UPROPERTY()
	FAggregatorRef	ChanceToApplyToTarget;

	UPROPERTY()
	FAggregatorRef	ChanceToExecuteOnGameplayEffect;

	// This should only be true if this is a stacking effect and at the top of its stack
	// (FIXME: should this be part of the spec or FActiveGameplayEffect?)
	UPROPERTY()
	bool bTopOfStack;

	// The spec needs to own these FModifierSpecs so that other people can keep TSharedPtr to it.
	// The stuff in this array is OWNED by this spec

	TArray<FModifierSpec> Modifiers;

	void MakeUnique();

	void InitModifiers(const FGlobalCurveDataOverride *CurveData, AActor *Owner, float Level);

	// returns the number of modifiers applied to the current GameplayEffectSpec by InSpec
	// returns -1 if the current GameplayEffectSpec prevents InSpec from being applied
	int32 ApplyModifiersFrom(const FGameplayEffectSpec &InSpec, const FModifierQualifier &QualifierContext);

	int32 ExecuteModifiersFrom(const FGameplayEffectSpec &InSpec, const FModifierQualifier &QualifierContext);

	// returns 1 if the modifier was applied, 0 otherwise
	int32 ApplyModifier(const FModifierSpec &InMod, const FModifierQualifier &QualifierContext, bool bApplyAsSnapshot);

	// returns true if modifiers applied to this GameplayEffectSpec should be a snapshot of the applied modifiers
	// returns false if modifiers applied to this GameplayEffectSpec should link to the applied modifiers
	bool ShouldApplyAsSnapshot(const FModifierQualifier &QualifierContext) const;

	FString ToSimpleString() const
	{
		return FString::Printf(TEXT("%s"), *Def->GetName());
	}

	void PrintAll() const;
};

/**
 * Active GameplayEffect instance
 *	-What GameplayEffect Spec
 *	-Start time
 *  -When to execute next
 *  -Replication callbacks
 *
 */
USTRUCT()
struct FActiveGameplayEffect : public FFastArraySerializerItem
{
	GENERATED_USTRUCT_BODY()

	FActiveGameplayEffect()
		: StartGameStateTime(0)
		, StartWorldTime(0.f)
	{
	}

	FActiveGameplayEffect(FActiveGameplayEffectHandle InHandle, const FGameplayEffectSpec &InSpec, float CurrentWorldTime, int32 InStartGameStateTime, FPredictionKey InPredictionKey)
		: Handle(InHandle)
		, Spec(InSpec)
		, PredictionKey(InPredictionKey)
		, StartGameStateTime(InStartGameStateTime)
		, StartWorldTime(CurrentWorldTime)
	{
		for (FModifierSpec &Mod : Spec.Modifiers)
		{
			Mod.Aggregator.Get()->ActiveHandle = InHandle;
		}
	}

	FActiveGameplayEffectHandle Handle;

	UPROPERTY()
	FGameplayEffectSpec Spec;

	UPROPERTY()
	FPredictionKey	PredictionKey;

	/** Game time this started */
	UPROPERTY()
	int32 StartGameStateTime;

	UPROPERTY(NotReplicated)
	float StartWorldTime;

	FOnActiveGameplayEffectRemoved	OnRemovedDelegate;

	FTimerHandle PeriodHandle;

	FTimerHandle DurationHandle;

	float GetTimeRemaining(float WorldTime)
	{
		float Duration = GetDuration();		
		return (Duration == UGameplayEffect::INFINITE_DURATION ? -1.f : Duration - (WorldTime - StartWorldTime));
	}
	
	float GetDuration() const
	{
		return Spec.GetDuration();
	}

	float GetPeriod() const
	{
		return Spec.GetPeriod();
	}

	bool CanBeStacked(const FActiveGameplayEffect& Other) const;

	void PrintAll() const;

	void PreReplicatedRemove(const struct FActiveGameplayEffectsContainer &InArray);
	void PostReplicatedAdd(const struct FActiveGameplayEffectsContainer &InArray);
	void PostReplicatedChange(const struct FActiveGameplayEffectsContainer &InArray);

	bool operator==(const FActiveGameplayEffect& Other)
	{
		return Handle == Other.Handle;
	}
};

/** Generic querying data structure for active GameplayEffects. Lets us ask things like: ** 
 *		-Give me duration/magnitude of active gameplay effects with these tags
 *		-
 */
USTRUCT()
struct FActiveGameplayEffectQuery
{
	GENERATED_USTRUCT_BODY()

	FActiveGameplayEffectQuery()
		: TagContainer(NULL)
	{
	}

	FActiveGameplayEffectQuery(const FGameplayTagContainer * InTagContainer)
		: TagContainer(InTagContainer)
	{
	}

	const FGameplayTagContainer *	TagContainer;
};

/**
 * Active GameplayEffects Container
 *	-Bucket of ActiveGameplayEffects
 *	-Needed for FFastArraySerialization
 *  
 * This should only be used by UAbilitySystemComponent. All of this could just live in UAbilitySystemComponent except that we need a distinct USTRUCT to implement FFastArraySerializer.
 *
 */
USTRUCT()
struct FActiveGameplayEffectsContainer : public FFastArraySerializer
{
	GENERATED_USTRUCT_BODY();

	friend struct FActiveGameplayEffect;

	FActiveGameplayEffectsContainer() : bNeedToRecalculateStacks(false), GameplayTagCountContainer(EGameplayTagMatchType::IncludeParentTags) {};

	UPROPERTY()
	TArray< FActiveGameplayEffect >	GameplayEffects;

	UAbilitySystemComponent *Owner;
	
	FActiveGameplayEffect& CreateNewActiveGameplayEffect(const FGameplayEffectSpec &Spec, FPredictionKey InPredictionKey);

	FActiveGameplayEffect* GetActiveGameplayEffect(const FActiveGameplayEffectHandle Handle);

	// returns true if none of the active effects provide immunity to Spec
	// returns false if one (or more) of the active effects provides immunity to Spec
	bool ApplyActiveEffectsTo(OUT FGameplayEffectSpec &Spec, const FModifierQualifier &QualifierContext) const;

	void ApplySpecToActiveEffectsAndAttributes(FGameplayEffectSpec &Spec, const FModifierQualifier &QualifierContext);
		
	void ExecuteActiveEffectsFrom(const FGameplayEffectSpec &Spec, const FModifierQualifier &QualifierContext);
	
	void ExecutePeriodicGameplayEffect(FActiveGameplayEffectHandle Handle);	// This should not be outward facing to the skill system API, should only be called by the owning AbilitySystemComponent

	void AddDependancyToAttribute(FGameplayAttribute Attribute, const TWeakPtr<FAggregator> InDependant);

	bool RemoveActiveGameplayEffect(FActiveGameplayEffectHandle Handle);
	
	float GetGameplayEffectDuration(FActiveGameplayEffectHandle Handle) const;

	float GetGameplayEffectMagnitude(FActiveGameplayEffectHandle Handle, FGameplayAttribute Attribute) const;

	// returns true if the handle points to an effect in this container that is not a stacking effect or an effect in this container that does stack and is applied by the current stacking rules
	// returns false if the handle points to an effect that is not in this container or is not applied because of the current stacking rules
	bool IsGameplayEffectActive(FActiveGameplayEffectHandle Handle) const;

	void PrintAllGameplayEffects() const;

	int32 GetNumGameplayEffects() const
	{
		return GameplayEffects.Num();
	}

	float GetGameplayEffectMagnitudeByTag(FActiveGameplayEffectHandle Handle, const FGameplayTag& InTag) const;

	/** Registered as a callback for when a PropertyAggregator is dirty and we need to update the corresponding GameplayAttribute */
	void OnPropertyAggregatorDirty(FAggregator* Aggregator, FGameplayAttribute Attribute);

	void CheckDuration(FActiveGameplayEffectHandle Handle);

	void StacksNeedToRecalculate();

	// recalculates all of the stacks in the current container
	void RecalculateStacking();

	bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms)
	{
		return FastArrayDeltaSerialize<FActiveGameplayEffect>(GameplayEffects, DeltaParms, *this);
	}

	void PreDestroy();

	bool bNeedToRecalculateStacks;

	// ------------------------------------------------

	void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const;

	bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const;

	bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer, bool bCountEmptyAsMatch = true) const;

	bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer, bool bCountEmptyAsMatch = true) const;

	// ------------------------------------------------

	bool CanApplyAttributeModifiers(const UGameplayEffect *GameplayEffect, float Level, AActor *Instigator);
	
	TArray<float> GetActiveEffectsTimeRemaining(const FActiveGameplayEffectQuery Query) const;

	TArray<float> GetActiveEffectsDuration(const FActiveGameplayEffectQuery Query) const;

	int32 GetGameStateTime() const;

	float GetWorldTime() const;

	void OnDurationAggregatorDirty(FAggregator* Aggregator, UAbilitySystemComponent* Owner, FActiveGameplayEffectHandle Handle);
	
	FOnGameplayEffectTagCountChanged& RegisterGameplayTagEvent(FGameplayTag Tag);

	FOnGameplayAttributeChange& RegisterGameplayAttributeEvent(FGameplayAttribute Attribute);

	bool HasReceivedEffectWithPredictedKey(FPredictionKey PredictionKey) const;

	bool HasPredictedEffectWithPredictedKey(FPredictionKey PredictionKey) const;
		
	void SetBaseAttributeValueFromReplication(FGameplayAttribute Attribute, float BaseBalue);

private:

	FTimerHandle StackHandle;

	FAggregatorRef& FindOrCreateAttributeAggregator(FGameplayAttribute Attribute);

	TMap<FGameplayAttribute, FAggregatorRef> OngoingPropertyEffects;

	TMap<FGameplayAttribute, FOnGameplayAttributeChange> AttributeChangeDelegates;

	FGameplayTagCountContainer GameplayTagCountContainer;

	void InternalUpdateNumericalAttribute(FGameplayAttribute Attribute, float NewValue, const FGameplayEffectModCallbackData* ModData);

	bool IsNetAuthority() const;

	/** Called internally to actually remove a GameplayEffect */
	bool InternalRemoveActiveGameplayEffect(int32 Idx);

	/** Called both in server side creation and replication creation/deletion */
	void InternalOnActiveGameplayEffectAdded(const FActiveGameplayEffect& Effect);
	void InternalOnActiveGameplayEffectRemoved(const FActiveGameplayEffect& Effect);

	void UpdateTagMap(const FGameplayTagContainer& Container, int32 CountDelta);
	void UpdateTagMap(const FGameplayTag& Tag, int32 CountDelta);
};

template<>
struct TStructOpsTypeTraits< FActiveGameplayEffectsContainer > : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};


/** Allows blueprints to generate a GameplayEffectSpec once and then reference it by handle, to apply it multiple times/multiple targets. */
USTRUCT(BlueprintType)
struct GAMEPLAYABILITIES_API FGameplayEffectSpecHandle
{
	GENERATED_USTRUCT_BODY()

	FGameplayEffectSpecHandle() { }
	FGameplayEffectSpecHandle(FGameplayEffectSpec* DataPtr)
		: Data(DataPtr)
	{

	}

	TSharedPtr<FGameplayEffectSpec>	Data;

	bool IsValidCache;

	void Clear()
	{
		Data.Reset();
	}

	bool IsValid() const
	{
		return Data.IsValid();
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		ABILITY_LOG(Fatal, TEXT("FGameplayEffectSpecHandle should not be NetSerialized"));
		return false;
	}

	/** Comparison operator */
	bool operator==(FGameplayEffectSpecHandle const& Other) const
	{
		// Both invalid structs or both valid and Pointer compare (???) // deep comparison equality
		bool bBothValid = IsValid() && Other.IsValid();
		bool bBothInvalid = !IsValid() && !Other.IsValid();
		return (bBothInvalid || (bBothValid && (Data.Get() == Other.Data.Get())));
	}

	/** Comparison operator */
	bool operator!=(FGameplayEffectSpecHandle const& Other) const
	{
		return !(FGameplayEffectSpecHandle::operator==(Other));
	}
};

template<>
struct TStructOpsTypeTraits<FGameplayEffectSpecHandle> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithCopy = true,		// Necessary so that TSharedPtr<FGameplayAbilityTargetData> Data is copied around
		WithNetSerializer = true,
		WithIdenticalViaEquality = true,
	};
};

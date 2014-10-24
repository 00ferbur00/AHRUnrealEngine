// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BodySetup.generated.h"

struct FDynamicMeshVertex;
class FMaterialRenderProxy;
class FPrimitiveDrawInterface;

namespace physx
{
	class PxConvexMesh;
	class PxTriangleMesh;
	class PxRigidActor;
}

UENUM()
enum ECollisionTraceFlag
{
	// default, we keep simple/complex separate for each test
	CTF_UseDefault UMETA(DisplayName="Default"),
	// use simple collision for complex collision test
	CTF_UseSimpleAsComplex UMETA(DisplayName="Use Simple Collision As Complex"),
	// use complex collision (per poly) for simple collision test
	CTF_UseComplexAsSimple UMETA(DisplayName="Use Complex Collision As Simple"),
	CTF_MAX,
};

UENUM()
enum EPhysicsType
{
	// follow owner option
	PhysType_Default UMETA(DisplayName="Default"),	
	// Do not follow owner, but make kinematic
	PhysType_Kinematic	UMETA(DisplayName="Kinematic"),		
	// Do not follow owner, but simulate
	PhysType_Simulated	UMETA(DisplayName="Simulated")	
};

UENUM()
namespace EBodyCollisionResponse
{
	enum Type
	{
		BodyCollision_Enabled UMETA(DisplayName="Enabled"), 
		BodyCollision_Disabled UMETA(DisplayName="Disabled")//, 
		//BodyCollision_Custom UMETA(DisplayName="Custom")
	};
}

/** One convex hull, used for simplified collision. */
USTRUCT()
struct FKConvexElem
{
	GENERATED_USTRUCT_BODY()

	/** Array of indices that make up the convex hull. */
	UPROPERTY()
	TArray<FVector> VertexData;

	/** Bounding box of this convex hull. */
	UPROPERTY()
	FBox ElemBox;

	/** Transform of this element */
	UPROPERTY()
	FTransform Transform;

		/** Convex mesh for this body, created from cooked data in CreatePhysicsMeshes */
		physx::PxConvexMesh*   ConvexMesh;	

		/** Convex mesh for this body, flipped across X, created from cooked data in CreatePhysicsMeshes */
		physx::PxConvexMesh*   ConvexMeshNegX;

		FKConvexElem() 
		: ElemBox(0)
		, Transform(FTransform::Identity)
		, ConvexMesh(NULL)
		, ConvexMeshNegX(NULL)
		{}

		ENGINE_API void	DrawElemWire(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, const FColor Color) const;

		void AddCachedSolidConvexGeom(TArray<FDynamicMeshVertex>& VertexBuffer, TArray<int32>& IndexBuffer, const FColor VertexColor) const;

		/** Reset the hull to empty all arrays */
		ENGINE_API void	Reset();

		/** Updates internal ElemBox based on current value of VertexData */
		ENGINE_API void	UpdateElemBox();

		/** Calculate a bounding box for this convex element with the specified transform and scale */
	ENGINE_API FBox	CalcAABB(const FTransform& BoneTM, const FVector& Scale3D) const;

		/** Utility for creating a convex hull from a set of planes. Will reset current state of this elem. */
		bool	HullFromPlanes(const TArray<FPlane>& InPlanes, const TArray<FVector>& SnapVerts);

		/** Returns the volume of this element */
		float GetVolume(const FVector& Scale) const;

		FTransform GetTransform() const
		{
			return Transform;
		};

		void SetTransform( const FTransform& InTransform )
		{
			ensure(InTransform.IsValid());
			Transform = InTransform;
		}

		friend FArchive& operator<<(FArchive& Ar,FKConvexElem& Elem);
	
	ENGINE_API void ScaleElem(FVector DeltaSize, float MinSize);
};

/** Sphere shape used for collision */
USTRUCT()
struct FKSphereElem
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FMatrix TM_DEPRECATED;

	UPROPERTY(Category=KSphereElem, VisibleAnywhere)
	FVector Center;

	UPROPERTY(Category=KSphereElem, VisibleAnywhere)
	float Radius;

	FKSphereElem() 
	: Center( FVector::ZeroVector )
	, Radius(1)
	{

	}

	FKSphereElem( float r ) 
	: Center( FVector::ZeroVector )
	, Radius(r)
	{

	}

	void Serialize( const FArchive& Ar );

	friend bool operator==( const FKSphereElem& LHS, const FKSphereElem& RHS )
	{
		return ( LHS.Center == RHS.Center &&
			LHS.Radius == RHS.Radius );
	}

	// Utility function that builds an FTransform from the current data
	FTransform GetTransform() const
	{
		return FTransform( Center );
	};

	void SetTransform(const FTransform& InTransform)
	{
		ensure(InTransform.IsValid());
		Center = InTransform.GetLocation();
	}

	FORCEINLINE float GetVolume(const FVector& Scale) const { return 1.3333f * PI * FMath::Pow(Radius * Scale.GetMin(), 3); }
	
	ENGINE_API void	DrawElemWire(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, float Scale, const FColor Color) const;
	ENGINE_API void	DrawElemSolid(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, float Scale, const FMaterialRenderProxy* MaterialRenderProxy) const;
	ENGINE_API void GetElemSolid(const FTransform& ElemTM, float Scale, const FMaterialRenderProxy* MaterialRenderProxy, int32 ViewIndex, class FMeshElementCollector& Collector) const;
	ENGINE_API FBox CalcAABB(const FTransform& BoneTM, float Scale) const;

	ENGINE_API void ScaleElem(FVector DeltaSize, float MinSize);
};

/** Box shape used for collision */
USTRUCT()
struct FKBoxElem
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FMatrix TM_DEPRECATED;

	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	FVector Center;

	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	FQuat Orientation;

	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	float X;

	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	float Y;

	/** length (not radius) */
	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	float Z;


	FKBoxElem()
	: Center( FVector::ZeroVector )
	, Orientation( FQuat::Identity )
	, X(1), Y(1), Z(1)
	{

	}

	FKBoxElem( float s )
	: Center( FVector::ZeroVector )
	, Orientation( FQuat::Identity )
	, X(s), Y(s), Z(s)
	{

	}

	FKBoxElem( float InX, float InY, float InZ ) 
	: Center( FVector::ZeroVector )
	, Orientation( FQuat::Identity )
	, X(InX), Y(InY), Z(InZ)

	{

	}

	void Serialize( const FArchive& Ar );

	friend bool operator==( const FKBoxElem& LHS, const FKBoxElem& RHS )
	{
		return ( LHS.Center == RHS.Center &&
			LHS.Orientation == RHS.Orientation &&
			LHS.X == RHS.X &&
			LHS.Y == RHS.Y &&
			LHS.Z == RHS.Z );
	};

	// Utility function that builds an FTransform from the current data
	FTransform GetTransform() const
	{
		return FTransform( Orientation, Center );
	};

	void SetTransform( const FTransform& InTransform )
	{
		ensure(InTransform.IsValid());
		Orientation = InTransform.GetRotation();
		Center = InTransform.GetLocation();
	}

	FORCEINLINE float GetVolume(const FVector& Scale) const { float MinScale = Scale.GetMin(); return (X * MinScale) * (Y * MinScale) * (Z * MinScale); }

	ENGINE_API void	DrawElemWire(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, float Scale, const FColor Color) const;
	ENGINE_API void	DrawElemSolid(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, float Scale, const FMaterialRenderProxy* MaterialRenderProxy) const;
	ENGINE_API void GetElemSolid(const FTransform& ElemTM, float Scale, const FMaterialRenderProxy* MaterialRenderProxy, int32 ViewIndex, FMeshElementCollector& Collector) const;
	ENGINE_API FBox CalcAABB(const FTransform& BoneTM, float Scale) const;

	ENGINE_API void ScaleElem(FVector DeltaSize, float MinSize);
};

/** Capsule shape used for collision */
USTRUCT()
struct FKSphylElem
{
	GENERATED_USTRUCT_BODY()

	/** The transform assumes the sphyl axis points down Z. */
	UPROPERTY()
	FMatrix TM_DEPRECATED;

	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	FVector Center;

	UPROPERTY(Category=KBoxElem, VisibleAnywhere)
	FQuat Orientation;

	UPROPERTY(Category=KSphylElem, VisibleAnywhere)
	float Radius;

	/** This is of line-segment ie. add Radius to both ends to find total length. */
	UPROPERTY(Category=KSphylElem, VisibleAnywhere)
	float Length;

	FKSphylElem()
	: Center( FVector::ZeroVector )
	, Orientation( FQuat::Identity )
	, Radius(1), Length(1)

	{

	}

	FKSphylElem( float InRadius, float InLength )
	: Center( FVector::ZeroVector )
	, Orientation( FQuat::Identity )
	, Radius(InRadius), Length(InLength)
	{

	}

	void Serialize( const FArchive& Ar );

	friend bool operator==( const FKSphylElem& LHS, const FKSphylElem& RHS )
	{
		return ( LHS.Center == RHS.Center &&
			LHS.Orientation == RHS.Orientation &&
			LHS.Radius == RHS.Radius &&
			LHS.Length == RHS.Length );
	};

	// Utility function that builds an FTransform from the current data
	FTransform GetTransform() const
	{
		return FTransform( Orientation, Center );
	};

	void SetTransform( const FTransform& InTransform )
	{
		ensure(InTransform.IsValid());
		Orientation = InTransform.GetRotation();
		Center = InTransform.GetLocation();
	}

	FORCEINLINE float GetVolume(const FVector& Scale) const { float ScaledRadius = Radius * Scale.GetMin(); return PI * FMath::Square(ScaledRadius) * ( 1.3333f * ScaledRadius + (Length * Scale.GetMin())); }

	ENGINE_API void	DrawElemWire(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, float Scale, const FColor Color) const;
	ENGINE_API void GetElemSolid(const FTransform& ElemTM, float Scale, const FMaterialRenderProxy* MaterialRenderProxy, int32 ViewIndex, FMeshElementCollector& Collector) const;
	ENGINE_API void	DrawElemSolid(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, float Scale, const FMaterialRenderProxy* MaterialRenderProxy) const;
	ENGINE_API FBox CalcAABB(const FTransform& BoneTM, float Scale) const;

	ENGINE_API void ScaleElem(FVector DeltaSize, float MinSize);
};

/** Container for an aggregate of collision shapes */
USTRUCT()
struct ENGINE_API FKAggregateGeom
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, editfixedsize, Category=KAggregateGeom)
	TArray<struct FKSphereElem> SphereElems;

	UPROPERTY(EditAnywhere, editfixedsize, Category=KAggregateGeom)
	TArray<struct FKBoxElem> BoxElems;

	UPROPERTY(EditAnywhere, editfixedsize, Category=KAggregateGeom)
	TArray<struct FKSphylElem> SphylElems;

	UPROPERTY(EditAnywhere, editfixedsize, Category=KAggregateGeom)
	TArray<struct FKConvexElem> ConvexElems;

	class FKConvexGeomRenderInfo* RenderInfo;

	FKAggregateGeom() 
	: RenderInfo(NULL)
	{}
	int32 GetElementCount() const
	{
		return SphereElems.Num() + SphylElems.Num() + BoxElems.Num() + ConvexElems.Num();
	}

	int32 GetElementCount(int32 Type) const;

	void EmptyElements()
	{
		BoxElems.Empty();
		ConvexElems.Empty();
		SphylElems.Empty();
		SphereElems.Empty();

		FreeRenderInfo();
	}

	

	void Serialize( const FArchive& Ar );

	void DrawAggGeom(class FPrimitiveDrawInterface* PDI, const FTransform& Transform, const FColor Color, const FMaterialRenderProxy* MatInst, bool bPerHullColor, bool bDrawSolid, bool bUseEditorDepthTest);

	void GetAggGeom(const FTransform& Transform, const FColor Color, const FMaterialRenderProxy* MatInst, bool bPerHullColor, bool bDrawSolid, bool bUseEditorDepthTest, int32 ViewIndex, class FMeshElementCollector& Collector) const;

	/** Release the RenderInfo (if its there) and safely clean up any resources. Call on the game thread. */
	void FreeRenderInfo();

	FBox CalcAABB(const FTransform& Transform) const;

	/**
		* Calculates a tight box-sphere bounds for the aggregate geometry; this is more expensive than CalcAABB
		* (tight meaning the sphere may be smaller than would be required to encompass the AABB, but all individual components lie within both the box and the sphere)
		*
		* @param Output The output box-sphere bounds calculated for this set of aggregate geometry
		*	@param LocalToWorld Transform
		*/
	void CalcBoxSphereBounds(FBoxSphereBounds& Output, const FTransform& LocalToWorld) const;

	/** Returns the volume of this element */
	float GetVolume(const FVector& Scale3D) const;
};

UCLASS(hidecategories=Object, MinimalAPI)
class UBodySetup : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Simplified collision representation of this  */
	UPROPERTY()
	struct FKAggregateGeom AggGeom;

	/** Used in the PhysicsAsset case. Associates this Body with Bone in a skeletal mesh. */
	UPROPERTY(Category=BodySetup, VisibleAnywhere)
	FName BoneName;

	/** 
	 *	If Unfixed it will use physics. If fixed, it will use kinematic. Default will inherit from OwnerComponent's behavior.
	 */
	UPROPERTY(EditAnywhere, Category=Physics)
	TEnumAsByte<EPhysicsType> PhysicsType;

	/** 
	 *	If true (and bEnableFullAnimWeightBodies in SkelMeshComp is true), the physics of this bone will always be blended into the skeletal mesh, regardless of what PhysicsWeight of the SkelMeshComp is. 
	 *	This is useful for bones that should always be physics, even when blending physics in and out for hit reactions (eg cloth or pony-tails).
	 */
	UPROPERTY()
	uint32 bAlwaysFullAnimWeight_DEPRECATED:1;

	/** 
	 *	Should this BodySetup be considered for the bounding box of the PhysicsAsset (and hence SkeletalMeshComponent).
	 *	There is a speed improvement from having less BodySetups processed each frame when updating the bounds.
	 */
	UPROPERTY(EditAnywhere, Category=BodySetup)
	uint32 bConsiderForBounds:1;

	/** 
	 *	If true, the physics of this mesh (only affects static meshes) will always contain ALL elements from the mesh - not just the ones enabled for collision. 
	 *	This is useful for forcing high detail collisions using the entire render mesh.
	 */
	UPROPERTY(Transient)
	uint32 bMeshCollideAll:1;

	/**
	*	If true, the physics triangle mesh will use double sided faces when doing scene queries.
	*	This is useful for planes and single sided meshes that need traces to work on both sides.
	*/
	UPROPERTY(EditAnywhere, Category=Physics)
	uint32 bDoubleSidedGeometry : 1;

	/**	Should we generate data necessary to support collision on normal (non-mirrored) versions of this body. */
	UPROPERTY()
	uint32 bGenerateNonMirroredCollision:1;

	/** Whether the cooked data is shared by multiple body setups. This is needed for per poly collision case where we don't want to duplicate cooked data, but still need multiple body setups for in place geometry changes */
	UPROPERTY()
	uint32 bSharedCookedData : 1;

	/** 
	 *	Should we generate data necessary to support collision on mirrored versions of this mesh. 
	 *	This halves the collision data size for this mesh, but disables collision on mirrored instances of the body.
	 */
	UPROPERTY()
	uint32 bGenerateMirroredCollision:1;

	/** Physical material to use for simple collision on this body. Encodes information about density, friction etc. */
	UPROPERTY(EditAnywhere, Category=Physics, meta=(DisplayName="Simple Collision Physical Material"))
	class UPhysicalMaterial* PhysMaterial;

	/** Collision Type for this body. This eventually changes response to collision to others **/
	UPROPERTY(EditAnywhere, Category=Collision)
	TEnumAsByte<enum EBodyCollisionResponse::Type> CollisionReponse;

	/** Collision Trace behavior - by default, it will keep simple(convex)/complex(per-poly) separate **/
	UPROPERTY(EditAnywhere, Category=Collision, meta=(DisplayName = "Collision Complexity"))
	TEnumAsByte<enum ECollisionTraceFlag> CollisionTraceFlag;

	/** Default properties of the body instance, copied into objects on instantiation, was URB_BodyInstance */
	UPROPERTY(EditAnywhere, Category=Collision, meta=(FullyExpand = "true"))
	struct FBodyInstance DefaultInstance;

	/** Custom walkable slope setting for this body. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Physics)
	struct FWalkableSlopeOverride WalkableSlopeOverride;

	UPROPERTY()
	float BuildScale_DEPRECATED;

	/** Build scale for this body setup (static mesh settings define this value) */
	UPROPERTY()
	FVector BuildScale3D;

	/** GUID used to uniquely identify this setup so it can be found in the DDC */
	FGuid BodySetupGuid;

	/** Cooked physics data for each format */
	FFormatContainer CookedFormatData;

	/** Cooked physics data override. This is needed in cases where some other body setup has the cooked data and you don't want to own it or copy it. See per poly skeletal mesh */
	FFormatContainer* CookedFormatDataOverride;

#if WITH_PHYSX
	/** Physics triangle mesh, created from cooked data in CreatePhysicsMeshes */
	physx::PxTriangleMesh* TriMesh;

	/** Physics triangle mesh, flipped across X, created from cooked data in CreatePhysicsMeshes */
	physx::PxTriangleMesh* TriMeshNegX;
#endif

	/** Flag used to know if we have created the physics convex and tri meshes from the cooked data yet */
	bool bCreatedPhysicsMeshes;

	/** Indicates whether this setup has any cooked collision data. */
	bool bHasCookedCollisionData;

public:
	// Begin UObject interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;
	virtual void FinishDestroy() override;
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR
	virtual SIZE_T GetResourceSize(EResourceSizeMode::Type Mode) override;
	// End UObject interface.

	//
	// UBodySetup interface.
	//
	ENGINE_API void CopyBodyPropertiesFrom(const UBodySetup* FromSetup);

	/** Add collision shapes from another body setup to this one */
	ENGINE_API void AddCollisionFrom(class UBodySetup* FromSetup);


	/** Create Physics meshes (ConvexMeshes, TriMesh & TriMeshNegX) from cooked data */
	/** Release Physics meshes (ConvexMeshes, TriMesh & TriMeshNegX). Must be called before the BodySetup is destroyed */
	ENGINE_API virtual void CreatePhysicsMeshes();

	/** Returns the volume of this element */
	ENGINE_API virtual float GetVolume(const FVector& Scale) const;

	/** Release Physics meshes (ConvexMeshes, TriMesh & TriMeshNegX) */
	ENGINE_API void ClearPhysicsMeshes();

	/** Calculates the mass. You can pass in the component where additional information is pulled from ( Scale, PhysMaterialOverride ) */
	ENGINE_API virtual float CalculateMass(const UPrimitiveComponent* Component = nullptr) const;

	/** Returns the physics material used for this body. If none, specified, returns the default engine material. */
	ENGINE_API class UPhysicalMaterial* GetPhysMaterial() const;

#if WITH_RUNTIME_PHYSICS_COOKING || WITH_EDITOR
	/** Clear all simple collision */
	ENGINE_API void RemoveSimpleCollision();

	/** 
	 * Rescales simple collision geometry.  Note you must recreate physics meshes after this 
	 *
	 * @param BuildScale	The scale to apply to the geometry
	 */
	ENGINE_API void RescaleSimpleCollision( FVector BuildScale );

	/** Invalidate physics data */
	ENGINE_API virtual void	InvalidatePhysicsData();	

	/**
	 * Converts a UModel to a set of convex hulls for simplified collision.  Any convex elements already in
	 * this BodySetup will be destroyed.  WARNING: the input model can have no single polygon or
	 * set of coplanar polygons which merge to more than FPoly::MAX_VERTICES vertices.
	 *
	 * @param		InModel					The input BSP.
	 * @param		bRemoveExisting			If true, clears any pre-existing collision
	 * @return								true on success, false on failure because of vertex count overflow.
	 */
	ENGINE_API void CreateFromModel(class UModel* InModel, bool bRemoveExisting);

#endif // WITH_RUNTIME_PHYSICS_COOKING || WITH_EDITOR

	/**
	 * Converts the skinned data of a skeletal mesh into a tri mesh collision. This is used for per poly scene queries and is quite expensive.
	 * In 99% of cases you should be fine using a physics asset created for the skeletal mesh
	 * @param	InSkeletalMeshComponent		The skeletal mesh component we'll be grabbing the skinning information from
	 */
	ENGINE_API void UpdateTriMeshVertices(const TArray<FVector> & NewPositions);

	/**
	 * Given a format name returns its cooked data.
	 *
	 * @param Format Physics format name.
	 * @return Cooked data or NULL of the data was not found.
	 */
	FByteBulkData* GetCookedData(FName Format);

#if WITH_PHYSX
	/** 
	 *   Add the shapes defined by this body setup to the supplied PxRigidBody. 
	 */
	void AddShapesToRigidActor(physx::PxRigidActor* PDestActor, FVector& Scale3D, const FTransform& RelativeTM = FTransform::Identity, TArray<physx::PxShape*>* NewShapes = NULL);

private:
	void AddSpheresToRigidActor(physx::PxRigidActor* PDestActor, const FTransform& RelativeTM, float MinScale, float MinScaleAbs, TArray<physx::PxShape*>* NewShapes) const;
	void AddBoxesToRigidActor(physx::PxRigidActor* PDestActor, const FTransform& RelativeTM, const FVector& Scale3D, const FVector& Scale3DAbs, TArray<physx::PxShape*>* NewShapes) const;
	void AddSphylsToRigidActor(physx::PxRigidActor* PDestActor, const FTransform& RelativeTM, const FVector& Scale3D, const FVector& Scale3DAbs, TArray<physx::PxShape*>* NewShapes) const;
	void AddConvexElemsToRigidActor(physx::PxRigidActor* PDestActor, const FTransform& RelativeTM, const FVector& Scale3D, const FVector& Scale3DAbs, TArray<physx::PxShape*>* NewShapes) const;
	void AddTriMeshToRigidActor(physx::PxRigidActor* PDestActor, const FVector& Scale3D, const FVector& Scale3DAbs) const;
#endif // WITH_PHYSX

};

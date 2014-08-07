// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	InstancedStaticMesh.cpp: Static mesh rendering code.
=============================================================================*/

#include "EnginePrivate.h"
#include "PhysicsPublic.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"

#include "Components/InteractiveFoliageComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/ModelComponent.h"
#include "Components/NiagaraComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/DrawSphereComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/VectorFieldComponent.h"
#include "PhysicsEngine/RadialForceComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/WindDirectionalSourceComponent.h"
#include "Components/TimelineComponent.h"
#include "Slate.h"
#include "NavDataGenerator.h"
#include "OnlineSubsystemUtils.h"
#include "AI/Navigation/RecastHelpers.h"

#include "StaticMeshResources.h"
#include "StaticMeshLight.h"
#include "SpeedTreeWind.h"
#include "ComponentInstanceDataCache.h"
#include "InstancedFoliage.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"

#if WITH_PHYSX
#include "PhysicsEngine/PhysXSupport.h"
#include "Collision/PhysXCollision.h"
#endif

// This must match the maximum a user could specify in the material (see 
// FHLSLMaterialTranslator::TextureCoordinate), otherwise the material will attempt 
// to look up a texture coordinate we didn't provide an element for.
static const int32 InstancedStaticMeshMaxTexCoord = 8;

/*-----------------------------------------------------------------------------
	FStaticMeshInstanceData
-----------------------------------------------------------------------------*/

/** The implementation of the static mesh instance data storage type. */
class FStaticMeshInstanceData :
	public FStaticMeshVertexDataInterface,
	public TResourceArray<FVector4,VERTEXBUFFER_ALIGNMENT>
{
public:

	typedef TResourceArray<FVector4,VERTEXBUFFER_ALIGNMENT> ArrayType;

	/**
	 * Constructor
	 * @param InNeedsCPUAccess - true if resource array data should be CPU accessible
	 */
	FStaticMeshInstanceData(bool InNeedsCPUAccess=false)
		:	TResourceArray<FVector4,VERTEXBUFFER_ALIGNMENT>(InNeedsCPUAccess)
	{
	}

	/**
	 * Resizes the vertex data buffer, discarding any data which no longer fits.
	 * @param NumVertices - The number of vertices to allocate the buffer for.
	 */
	virtual void ResizeBuffer(uint32 NumInstances)
	{
		checkf(0, TEXT("ArrayType::Add is not supported on all platforms"));
	}

	virtual uint32 GetStride() const
	{
		const uint32 VectorsPerInstance = 7;
		return sizeof(FVector4) * VectorsPerInstance;
	}
	virtual uint8* GetDataPointer()
	{
		return (uint8*)&(*this)[0];
	}
	virtual FResourceArrayInterface* GetResourceArray()
	{
		return this;
	}
	virtual void Serialize(FArchive& Ar)
	{
		TResourceArray<FVector4,VERTEXBUFFER_ALIGNMENT>::BulkSerialize(Ar);
	}

	void Set(const TArray<FVector4>& RawData)
	{
		*((ArrayType*)this) = TArray<FVector4,TAlignedHeapAllocator<VERTEXBUFFER_ALIGNMENT> >(RawData);
	}
};


/*-----------------------------------------------------------------------------
	FStaticMeshInstanceBuffer
-----------------------------------------------------------------------------*/


/** A vertex buffer of positions. */
class FStaticMeshInstanceBuffer : public FVertexBuffer
{
public:

	/** Default constructor. */
	FStaticMeshInstanceBuffer();

	/** Destructor. */
	~FStaticMeshInstanceBuffer();

	/** Delete existing resources */
	void CleanUp();

	/**
	 * Initializes the buffer with the component's data.
	 * @param InComponent - The owning component
	 * @param InHitProxies - Array of hit proxies for each instance, if desired.
	 */
	void Init(UInstancedStaticMeshComponent* InComponent, const TArray<TRefCountPtr<HHitProxy> >& InHitProxies);

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar, FStaticMeshInstanceBuffer& VertexBuffer);

	/**
	 * Specialized assignment operator, only used when importing LOD's. 
	 */
	void operator=(const FStaticMeshInstanceBuffer &Other);

	// Other accessors.
	FORCEINLINE uint32 GetStride() const
	{
		return Stride;
	}
	FORCEINLINE uint32 GetNumInstances() const
	{
		return NumInstances;
	}

	const void* GetRawData() const
	{
		return InstanceData->GetDataPointer();
	}

	// FRenderResource interface.
	virtual void InitRHI() override;
	virtual FString GetFriendlyName() const { return TEXT("Static-mesh instances"); }

private:

	/** The vertex data storage type */
	FStaticMeshInstanceData* InstanceData;

	/** The cached vertex stride. */
	uint32 Stride;

	/** The cached number of instances. */
	uint32 NumInstances;

	/** Allocates the vertex data storage type. */
	void AllocateData();
};


FStaticMeshInstanceBuffer::FStaticMeshInstanceBuffer():
	InstanceData(NULL)
{
}

FStaticMeshInstanceBuffer::~FStaticMeshInstanceBuffer()
{
	CleanUp();
}

/** Delete existing resources */
void FStaticMeshInstanceBuffer::CleanUp()
{
	if (InstanceData)
	{
		delete InstanceData;
		InstanceData = NULL;
	}
}

/**
 * Initializes the buffer with the component's data.
 * @param InComponent - The owning component
 */
void FStaticMeshInstanceBuffer::Init(UInstancedStaticMeshComponent* InComponent, const TArray<TRefCountPtr<HHitProxy> >& InHitProxies)
{
	NumInstances = InComponent->PerInstanceSMData.Num();

	// Allocate the vertex data storage type.
	AllocateData();

	// We cannot write directly to the data on all platforms,
	// so we make a TArray of the right type, then assign it
	TArray<FVector4> RawData;
	check( GetStride() % sizeof(FVector4) == 0 );
	RawData.Empty(NumInstances * GetStride() / sizeof(FVector4));

	// @todo: Make LD-customizable per component?
	const float RandomInstanceIDBase = 0.0f;
	const float RandomInstanceIDRange = 1.0f;

	// Setup our random number generator such that random values are generated consistently for any
	// given instance index between reattaches
	FRandomStream RandomStream( InComponent->InstancingRandomSeed );

	FMatrix LocalToWorld = InComponent->GetComponentToWorld().ToMatrixWithScale();

	for (uint32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
	{
		const FInstancedStaticMeshInstanceData& Instance = InComponent->PerInstanceSMData[InstanceIndex];

		// X, Y	: Shadow map UV bias
		// Z, W : Encoded HitProxy ID.
		float Z = 0.f;
		float W = 0.f;
		if( InHitProxies.Num() == NumInstances )
		{
			FColor HitProxyColor = InHitProxies[InstanceIndex]->Id.GetColor();
			Z = (float)HitProxyColor.R;
			W = (float)HitProxyColor.G * 256.f + (float)HitProxyColor.B;
		}
#if WITH_EDITOR
		// Record if the instance is selected
		if( InstanceIndex < (uint32)InComponent->SelectedInstances.Num() && InComponent->SelectedInstances[InstanceIndex] )
		{
			Z += 256.f;
		}
#endif
		RawData.Add( FVector4( Instance.ShadowmapUVBias.X, Instance.ShadowmapUVBias.Y, Z, W ) );

		// Instance -> local matrix.  Every mesh instance has it's own transformation into
		// the actor's coordinate space.
		{
			const FMatrix Transpose = Instance.Transform.GetTransposed();
						
			RawData.Add( FVector4(Transpose.M[0][0], Transpose.M[0][1], Transpose.M[0][2], Transpose.M[0][3]) );
			RawData.Add( FVector4(Transpose.M[1][0], Transpose.M[1][1], Transpose.M[1][2], Transpose.M[1][3]) );
			RawData.Add( FVector4(Transpose.M[2][0], Transpose.M[2][1], Transpose.M[2][2], Transpose.M[2][3]) );
		}

		// Instance -> local rotation matrix (3x3)
		{
			const float RandomInstanceID = RandomInstanceIDBase + RandomStream.GetFraction() * RandomInstanceIDRange;
			// hide the offset (bias) of the lightmap and the per-instance random id in the matrix's w
			const FMatrix Transpose = Instance.Transform.Inverse().GetTransposed();
			
			RawData.Add( FVector4(Transpose.M[0][0], Transpose.M[0][1], Transpose.M[0][2], Instance.LightmapUVBias.X) );
			RawData.Add( FVector4(Transpose.M[1][0], Transpose.M[1][1], Transpose.M[1][2], Instance.LightmapUVBias.Y) );
			RawData.Add( FVector4(Transpose.M[2][0], Transpose.M[2][1], Transpose.M[2][2], RandomInstanceID) );
		}
	}

	// Allocate the vertex data buffer.
	InstanceData->Set(RawData);
}

/** Serializer. */
FArchive& operator<<(FArchive& Ar,FStaticMeshInstanceBuffer& InstanceBuffer)
{
	Ar << InstanceBuffer.Stride << InstanceBuffer.NumInstances;

	if(Ar.IsLoading())
	{
		// Allocate the vertex data storage type.
		InstanceBuffer.AllocateData();
	}

	// Serialize the vertex data.
	InstanceBuffer.InstanceData->Serialize(Ar);

	return Ar;
}

/**
 * Specialized assignment operator, only used when importing LOD's.  
 */
void FStaticMeshInstanceBuffer::operator=(const FStaticMeshInstanceBuffer &Other)
{
	checkf(0, TEXT("Unexpected assignment call"));
}

void FStaticMeshInstanceBuffer::InitRHI()
{
	check(InstanceData);
	FResourceArrayInterface* ResourceArray = InstanceData->GetResourceArray();
	if(ResourceArray->GetResourceDataSize())
	{
		// Create the vertex buffer.
		FRHIResourceCreateInfo CreateInfo(ResourceArray);
		VertexBufferRHI = RHICreateVertexBuffer(ResourceArray->GetResourceDataSize(),BUF_Static, CreateInfo);
	}
}

void FStaticMeshInstanceBuffer::AllocateData()
{
	// Clear any old VertexData before allocating.
	CleanUp();

	const bool bInstanced = RHISupportsInstancing(GRHIShaderPlatform);
	const bool bNeedsCPUAccess = !bInstanced;
	InstanceData = new FStaticMeshInstanceData(bNeedsCPUAccess);
	// Calculate the vertex stride.
	Stride = InstanceData->GetStride();
}



/*-----------------------------------------------------------------------------
	FInstancedStaticMeshVertexFactory
-----------------------------------------------------------------------------*/

struct FInstancingUserData
{
	struct FInstanceStream
	{
		FVector4 InstanceShadowmapUVBias;
		FVector4 InstanceTransform[3];
		FVector4 InstanceInverseTransform[3];
	};
	class FInstancedStaticMeshRenderData* RenderData;

	int32 StartCullDistance;
	int32 EndCullDistance;

	bool bRenderSelected;
	bool bRenderUnselected;
};

/**
 * A vertex factory for instanced static meshes
 */
struct FInstancedStaticMeshVertexFactory : public FLocalVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FInstancedStaticMeshVertexFactory);
public:
	struct DataType : public FLocalVertexFactory::DataType
	{
		/** The stream to read shadow map bias (and random instance ID) from. */
		FVertexStreamComponent InstancedShadowMapBiasComponent;

		/** The stream to read the mesh transform from. */
		FVertexStreamComponent InstancedTransformComponent[3];

		/** The stream to read the inverse transform, as well as the Lightmap Bias in 0/1 */
		FVertexStreamComponent InstancedInverseTransformComponent[3];
	};

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	static bool ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType);

	/**
	 * Modify compile environment to enable instancing
	 * @param OutEnvironment - shader compile environment to modify
	 */
	static void ModifyCompilationEnvironment(EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING"),TEXT("1"));
		const bool bInstanced = RHISupportsInstancing(Platform);
		OutEnvironment.SetDefine(TEXT("USE_INSTANCING_EMULATED"), bInstanced ? TEXT("0") : TEXT("1"));
	}

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const DataType& InData)
	{
		Data = InData;
		UpdateRHI();
	}

	/**
	 * Copy the data from another vertex factory
	 * @param Other - factory to copy from
	 */
	void Copy(const FInstancedStaticMeshVertexFactory& Other);

	// FRenderResource interface.
	virtual void InitRHI() override;

	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	/** Make sure we account for changes in the signature of GetStaticBatchElementVisibility() */
	static CONSTEXPR uint32 NumBitsForVisibilityMask()
	{		
		return 8 * sizeof(decltype(((FInstancedStaticMeshVertexFactory*)nullptr)->GetStaticBatchElementVisibility(FSceneView(FSceneViewInitOptions()), nullptr)));
	}

	/**
	* Get a bitmask representing the visibility of each FMeshBatch element.
	*/
	virtual uint64 GetStaticBatchElementVisibility(const class FSceneView& View, const struct FMeshBatch* Batch) const override
	{
		uint32 NumElements = FMath::Min((uint32)Batch->Elements.Num(), NumBitsForVisibilityMask());
		return (1ULL << (uint64)NumElements) - 1ULL;
	}

private:
	DataType Data;
};



/**
 * Should we cache the material's shadertype on this platform with this vertex factory? 
 */
bool FInstancedStaticMeshVertexFactory::ShouldCache(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
{
	return (Material->IsUsedWithInstancedStaticMeshes() || Material->IsSpecialEngineMaterial())
			&& FLocalVertexFactory::ShouldCache(Platform, Material, ShaderType);
}


/**
 * Copy the data from another vertex factory
 * @param Other - factory to copy from
 */
void FInstancedStaticMeshVertexFactory::Copy(const FInstancedStaticMeshVertexFactory& Other)
{
	ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		FInstancedStaticMeshVertexFactoryCopyData,
		FInstancedStaticMeshVertexFactory*,VertexFactory,this,
		const DataType*,DataCopy,&Other.Data,
	{
		VertexFactory->Data = *DataCopy;
	});
	BeginUpdateResourceRHI(this);
}

void FInstancedStaticMeshVertexFactory::InitRHI()
{
	const bool bInstanced = RHISupportsInstancing(GRHIShaderPlatform);

	// If the vertex buffer containing position is not the same vertex buffer containing the rest of the data,
	// then initialize PositionStream and PositionDeclaration.
	if(Data.PositionComponent.VertexBuffer != Data.TangentBasisComponents[0].VertexBuffer)
	{
		FVertexDeclarationElementList PositionOnlyStreamElements;
		PositionOnlyStreamElements.Add(AccessPositionStreamComponent(Data.PositionComponent,0));

		if (bInstanced)
		{
			// toss in the instanced location stream
			PositionOnlyStreamElements.Add(AccessPositionStreamComponent(Data.InstancedTransformComponent[0],9));
			PositionOnlyStreamElements.Add(AccessPositionStreamComponent(Data.InstancedTransformComponent[1],10));
			PositionOnlyStreamElements.Add(AccessPositionStreamComponent(Data.InstancedTransformComponent[2],11));
		}
		InitPositionDeclaration(PositionOnlyStreamElements);
	}

	FVertexDeclarationElementList Elements;
	if(Data.PositionComponent.VertexBuffer != NULL)
	{
		Elements.Add(AccessStreamComponent(Data.PositionComponent,0));
	}

	// only tangent,normal are used by the stream. the binormal is derived in the shader
	uint8 TangentBasisAttributes[2] = { 1, 2 };
	for(int32 AxisIndex = 0;AxisIndex < 2;AxisIndex++)
	{
		if(Data.TangentBasisComponents[AxisIndex].VertexBuffer != NULL)
		{
			Elements.Add(AccessStreamComponent(Data.TangentBasisComponents[AxisIndex],TangentBasisAttributes[AxisIndex]));
		}
	}

	if(Data.ColorComponent.VertexBuffer)
	{
		Elements.Add(AccessStreamComponent(Data.ColorComponent,3));
	}
	else
	{
		//If the mesh has no color component, set the null color buffer on a new stream with a stride of 0.
		//This wastes 4 bytes of bandwidth per vertex, but prevents having to compile out twice the number of vertex factories.
		FVertexStreamComponent NullColorComponent(&GNullColorVertexBuffer, 0, 0, VET_Color);
		Elements.Add(AccessStreamComponent(NullColorComponent,3));
	}

	if(Data.TextureCoordinates.Num())
	{
		const int32 BaseTexCoordAttribute = 4;
		for(int32 CoordinateIndex = 0;CoordinateIndex < Data.TextureCoordinates.Num();CoordinateIndex++)
		{
			Elements.Add(AccessStreamComponent(
				Data.TextureCoordinates[CoordinateIndex],
				BaseTexCoordAttribute + CoordinateIndex
				));
		}

		for(int32 CoordinateIndex = Data.TextureCoordinates.Num(); CoordinateIndex < (InstancedStaticMeshMaxTexCoord + 1) / 2; CoordinateIndex++)
		{
			Elements.Add(AccessStreamComponent(
				Data.TextureCoordinates[Data.TextureCoordinates.Num() - 1],
				BaseTexCoordAttribute + CoordinateIndex
				));
		}
	}

	if(Data.LightMapCoordinateComponent.VertexBuffer)
	{
		Elements.Add(AccessStreamComponent(Data.LightMapCoordinateComponent,15));
	}
	else if(Data.TextureCoordinates.Num())
	{
		Elements.Add(AccessStreamComponent(Data.TextureCoordinates[0],15));
	}

	// toss in the instanced location stream
	if (bInstanced)
	{
		Elements.Add(AccessStreamComponent(Data.InstancedShadowMapBiasComponent,8));
		Elements.Add(AccessStreamComponent(Data.InstancedTransformComponent[0],9));
		Elements.Add(AccessStreamComponent(Data.InstancedTransformComponent[1],10));
		Elements.Add(AccessStreamComponent(Data.InstancedTransformComponent[2],11));
		Elements.Add(AccessStreamComponent(Data.InstancedInverseTransformComponent[0],12));
		Elements.Add(AccessStreamComponent(Data.InstancedInverseTransformComponent[1],13));
		Elements.Add(AccessStreamComponent(Data.InstancedInverseTransformComponent[2],14));
	}

	// we don't need per-vertex shadow or lightmap rendering
	InitDeclaration(Elements,Data);
}

class FInstancedStaticMeshVertexFactoryShaderParameters : public FLocalVertexFactoryShaderParameters
{
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		FLocalVertexFactoryShaderParameters::Bind(ParameterMap);

		InstancingFadeOutParamsParameter.Bind(ParameterMap, TEXT("InstancingFadeOutParams"));
		CPUInstanceShadowMapBias.Bind(ParameterMap, TEXT("CPUInstanceShadowMapBias"));
		CPUInstanceTransform.Bind(ParameterMap, TEXT("CPUInstanceTransform"));
		CPUInstanceInverseTransform.Bind(ParameterMap, TEXT("CPUInstanceInverseTransform"));
	}

	virtual void SetMesh(FRHICommandList& RHICmdList, FShader* VertexShader,const class FVertexFactory* VertexFactory,const class FSceneView& View,const struct FMeshBatchElement& BatchElement,uint32 DataFlags) const override;

	void Serialize(FArchive& Ar)
	{
		FLocalVertexFactoryShaderParameters::Serialize(Ar);
		Ar << InstancingFadeOutParamsParameter;
		Ar << CPUInstanceShadowMapBias;
		Ar << CPUInstanceTransform;
		Ar << CPUInstanceInverseTransform;
	}

	virtual uint32 GetSize() const { return sizeof(*this); }

private:
	FShaderParameter InstancingFadeOutParamsParameter;

	FShaderParameter CPUInstanceShadowMapBias;
	FShaderParameter CPUInstanceTransform;
	FShaderParameter CPUInstanceInverseTransform;
};

FVertexFactoryShaderParameters* FInstancedStaticMeshVertexFactory::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
	return ShaderFrequency == SF_Vertex ? new FInstancedStaticMeshVertexFactoryShaderParameters() : NULL;
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FInstancedStaticMeshVertexFactory,"LocalVertexFactory",true,true,true,true,true);




/*-----------------------------------------------------------------------------
	FInstancedStaticMeshRenderData
-----------------------------------------------------------------------------*/

class FInstancedStaticMeshRenderData
{
public:

	FInstancedStaticMeshRenderData(UInstancedStaticMeshComponent* InComponent)
	  : Component(InComponent)
	  , LODModels(Component->StaticMesh->RenderData->LODResources)
	{
		// Allocate the vertex factories for each LOD
		for( int32 LODIndex=0;LODIndex<LODModels.Num();LODIndex++ )
		{
			new(VertexFactories) FInstancedStaticMeshVertexFactory;
		}

		// Create hit proxies for each instance if the component wants
		if( GIsEditor && InComponent->bHasPerInstanceHitProxies )
		{
			for( int32 InstanceIdx=0;InstanceIdx<Component->PerInstanceSMData.Num();InstanceIdx++ )
			{
				HitProxies.Add( new HInstancedStaticMeshInstance(InComponent, InstanceIdx) );
			}
		}

		// initialize the instance buffer from the component's instances
		InstanceBuffer.Init(Component, HitProxies);
		InitResources();
	}

	~FInstancedStaticMeshRenderData()
	{
		ReleaseResources();
	}

	void InitResources()
	{
		BeginInitResource(&InstanceBuffer);

		// Initialize the static mesh's vertex factory.
		ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
			CallInitStaticMeshVertexFactory,
			TArray<FInstancedStaticMeshVertexFactory>*,VertexFactories,&VertexFactories,
			FInstancedStaticMeshRenderData*,InstancedRenderData,this,
			UStaticMesh*,Parent,Component->StaticMesh,
		{
			InitStaticMeshVertexFactories( VertexFactories, InstancedRenderData, Parent );
		});

		for( int32 LODIndex=0;LODIndex<VertexFactories.Num();LODIndex++ )
		{
			BeginInitResource(&VertexFactories[LODIndex]);
		}

		// register SpeedTree wind with the scene
		if (Component->StaticMesh->SpeedTreeWind.IsValid())
		{
			for (int32 LODIndex = 0; LODIndex < LODModels.Num(); LODIndex++)
			{
				Component->GetScene()->AddSpeedTreeWind(&VertexFactories[LODIndex], Component->StaticMesh);
			}
		}
	}

	void ReleaseResources()
	{
		// unregister SpeedTree wind with the scene
		if (Component && Component->GetScene() && Component->StaticMesh && Component->StaticMesh->SpeedTreeWind.IsValid())
		{
			for (int32 LODIndex = 0; LODIndex < VertexFactories.Num(); LODIndex++)
			{
				Component->GetScene()->RemoveSpeedTreeWind(&VertexFactories[LODIndex], Component->StaticMesh);
			}
		}

		InstanceBuffer.ReleaseResource();
		for( int32 LODIndex=0;LODIndex<VertexFactories.Num();LODIndex++ )
		{
			VertexFactories[LODIndex].ReleaseResource();
		}
	}

	static void InitStaticMeshVertexFactories(
		TArray<FInstancedStaticMeshVertexFactory>* VertexFactories,
		FInstancedStaticMeshRenderData* InstancedRenderData,
		UStaticMesh* Parent);

	/** Source component */
	UInstancedStaticMeshComponent* Component;

	/** Instance buffer */
	FStaticMeshInstanceBuffer InstanceBuffer;

	/** Vertex factory */
	TArray<FInstancedStaticMeshVertexFactory> VertexFactories;

	/** LOD render data from the static mesh. */
	TIndirectArray<FStaticMeshLODResources>& LODModels;

	/** Hit proxies for the instances */
	TArray<TRefCountPtr<HHitProxy> > HitProxies;
};

void FInstancedStaticMeshRenderData::InitStaticMeshVertexFactories(
		TArray<FInstancedStaticMeshVertexFactory>* VertexFactories,
		FInstancedStaticMeshRenderData* InstancedRenderData,
		UStaticMesh* Parent)
{
	const bool bInstanced = RHISupportsInstancing(GRHIShaderPlatform);

	for( int32 LODIndex=0;LODIndex<VertexFactories->Num(); LODIndex++ )
	{
		const FStaticMeshLODResources* RenderData = &InstancedRenderData->LODModels[LODIndex];
						
		FInstancedStaticMeshVertexFactory::DataType Data;
		Data.PositionComponent = FVertexStreamComponent(
			&RenderData->PositionVertexBuffer,
			STRUCT_OFFSET(FPositionVertex,Position),
			RenderData->PositionVertexBuffer.GetStride(),
			VET_Float3
			);
		Data.TangentBasisComponents[0] = FVertexStreamComponent(
			&RenderData->VertexBuffer,
			STRUCT_OFFSET(FStaticMeshFullVertex,TangentX),
			RenderData->VertexBuffer.GetStride(),
			VET_PackedNormal
			);
		Data.TangentBasisComponents[1] = FVertexStreamComponent(
			&RenderData->VertexBuffer,
			STRUCT_OFFSET(FStaticMeshFullVertex,TangentZ),
			RenderData->VertexBuffer.GetStride(),
			VET_PackedNormal
			);


		if( RenderData->ColorVertexBuffer.GetNumVertices() > 0 )
		{
			Data.ColorComponent = FVertexStreamComponent(
				&RenderData->ColorVertexBuffer,
				0,	// Struct offset to color
				RenderData->ColorVertexBuffer.GetStride(),
				VET_Color
				);
		}

		Data.TextureCoordinates.Empty();
		// Only bind InstancedStaticMeshMaxTexCoord, even if the mesh has more.
		int32 NumTexCoords = FMath::Min<int32>((int32)RenderData->VertexBuffer.GetNumTexCoords(), InstancedStaticMeshMaxTexCoord);
		if( !RenderData->VertexBuffer.GetUseFullPrecisionUVs() )
		{
			int32 UVIndex;
			for (UVIndex = 0; UVIndex < NumTexCoords - 1; UVIndex += 2)
			{
				Data.TextureCoordinates.Add(FVertexStreamComponent(
					&RenderData->VertexBuffer,
					STRUCT_OFFSET(TStaticMeshFullVertexFloat16UVs<MAX_STATIC_TEXCOORDS>, UVs) + sizeof(FVector2DHalf)* UVIndex,
					RenderData->VertexBuffer.GetStride(),
					VET_Half4
					));
			}
			// possible last UV channel if we have an odd number
			if( UVIndex < NumTexCoords )
			{
				Data.TextureCoordinates.Add(FVertexStreamComponent(
					&RenderData->VertexBuffer,
					STRUCT_OFFSET(TStaticMeshFullVertexFloat16UVs<MAX_STATIC_TEXCOORDS>,UVs) + sizeof(FVector2DHalf) * UVIndex,
					RenderData->VertexBuffer.GetStride(),
					VET_Half2
					));
			}

			if (Parent->LightMapCoordinateIndex >= 0 && Parent->LightMapCoordinateIndex < NumTexCoords)
			{
#if 0
				//@todo UE4 foliage - static lighting/shadowing?
				Data.ShadowMapCoordinateComponent = FVertexStreamComponent(
					&RenderData->VertexBuffer,
					STRUCT_OFFSET(TStaticMeshFullVertexFloat16UVs<MAX_STATIC_TEXCOORDS>, UVs) + sizeof(FVector2DHalf)* Parent->LightMapCoordinateIndex,
					RenderData->VertexBuffer.GetStride(),
					VET_Half2
					);
#endif
			}
		}
		else
		{
			int32 UVIndex;
			for (UVIndex = 0; UVIndex < NumTexCoords - 1; UVIndex += 2)
			{
				Data.TextureCoordinates.Add(FVertexStreamComponent(
					&RenderData->VertexBuffer,
					STRUCT_OFFSET(TStaticMeshFullVertexFloat32UVs<MAX_STATIC_TEXCOORDS>, UVs) + sizeof(FVector2D)* UVIndex,
					RenderData->VertexBuffer.GetStride(),
					VET_Float4
					));
			}
			// possible last UV channel if we have an odd number
			if (UVIndex < NumTexCoords)
			{
				Data.TextureCoordinates.Add(FVertexStreamComponent(
					&RenderData->VertexBuffer,
					STRUCT_OFFSET(TStaticMeshFullVertexFloat32UVs<MAX_STATIC_TEXCOORDS>,UVs) + sizeof(FVector2D) * UVIndex,
					RenderData->VertexBuffer.GetStride(),
					VET_Float2
					));
			}

			if (Parent->LightMapCoordinateIndex >= 0 && Parent->LightMapCoordinateIndex < NumTexCoords)
			{
#if 0
				//@todo UE4 foliage - static lighting/shadowing?
				Data.ShadowMapCoordinateComponent = FVertexStreamComponent(
					&RenderData->VertexBuffer,
					STRUCT_OFFSET(TStaticMeshFullVertexFloat32UVs<InstancedStaticMeshMaxTexCoord>, UVs) + sizeof(FVector2D)* Parent->LightMapCoordinateIndex,
					RenderData->VertexBuffer.GetStride(),
					VET_Float2
					);
#endif
			}
		}

		if (bInstanced)
		{
			// Shadow map bias (and random instance ID)
			int32 CurInstanceBufferOffset = 0;
			Data.InstancedShadowMapBiasComponent = FVertexStreamComponent(
				&InstancedRenderData->InstanceBuffer,
				CurInstanceBufferOffset, 
				InstancedRenderData->InstanceBuffer.GetStride(),
				VET_Float4,
				true
				);
			CurInstanceBufferOffset += sizeof(float) * 4;

			for (int32 MatrixRow = 0; MatrixRow < 3; MatrixRow++)
			{
				Data.InstancedTransformComponent[MatrixRow] = FVertexStreamComponent(
					&InstancedRenderData->InstanceBuffer,
					CurInstanceBufferOffset, 
					InstancedRenderData->InstanceBuffer.GetStride(),
					VET_Float4,
					true
					);
				CurInstanceBufferOffset += sizeof(float) * 4;
			}

			for (int32 MatrixRow = 0; MatrixRow < 3; MatrixRow++)
			{
				Data.InstancedInverseTransformComponent[MatrixRow] = FVertexStreamComponent(
					&InstancedRenderData->InstanceBuffer,
					CurInstanceBufferOffset, 
					InstancedRenderData->InstanceBuffer.GetStride(),
					VET_Float4,
					true
					);
				CurInstanceBufferOffset += sizeof(float) * 4;
			}
		}

		// Assign to the vertex factory for this LOD.
		FInstancedStaticMeshVertexFactory& VertexFactory = (*VertexFactories)[LODIndex];
		VertexFactory.SetData(Data);
	}
}



/*-----------------------------------------------------------------------------
	FInstancedStaticMeshSceneProxy
-----------------------------------------------------------------------------*/

class FInstancedStaticMeshSceneProxy : public FStaticMeshSceneProxy
{
public:

	FInstancedStaticMeshSceneProxy(UInstancedStaticMeshComponent* InComponent)
	:	FStaticMeshSceneProxy(InComponent)
	,	InstancedRenderData(InComponent)
#if WITH_EDITOR
	,	bHasSelectedInstances(InComponent->SelectedInstances.Num() > 0)
#endif
	{
#if WITH_EDITOR
		if( bHasSelectedInstances )
		{
			// if we have selected indices, mark scene proxy as selected.
			SetSelection_GameThread(true);
		}
#endif
		// Make sure all the materials are okay to be rendered as an instanced mesh.
		for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
		{
			FStaticMeshSceneProxy::FLODInfo& LODInfo = LODs[LODIndex];
			for (int32 SectionIndex = 0; SectionIndex < LODInfo.Sections.Num(); SectionIndex++)
			{
				FStaticMeshSceneProxy::FLODInfo::FSectionInfo& Section = LODInfo.Sections[SectionIndex];
				if (!Section.Material->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes))
				{
					Section.Material = UMaterial::GetDefaultMaterial(MD_Surface);
				}
			}
		}

		check(InstancedRenderData.InstanceBuffer.GetStride() == sizeof(FInstancingUserData::FInstanceStream));

		const bool bInstanced = RHISupportsInstancing(GRHIShaderPlatform);

		// Copy the parameters for LOD - all instances
		UserData_AllInstances.StartCullDistance = InComponent->InstanceStartCullDistance;
		UserData_AllInstances.EndCullDistance = InComponent->InstanceEndCullDistance;
		UserData_AllInstances.bRenderSelected = true;
		UserData_AllInstances.bRenderUnselected = true;
		UserData_AllInstances.RenderData = bInstanced ? nullptr : &InstancedRenderData;

		// selected only
		UserData_SelectedInstances = UserData_AllInstances;
		UserData_SelectedInstances.bRenderUnselected = false;

		// unselected only
		UserData_DeselectedInstances = UserData_AllInstances;
		UserData_DeselectedInstances.bRenderSelected = false;
	}

	// FPrimitiveSceneProxy interface.
	
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) override
	{
		FPrimitiveViewRelevance Result;
		if(View->Family->EngineShowFlags.InstancedStaticMeshes)
		{
			Result = FStaticMeshSceneProxy::GetViewRelevance(View);
#if WITH_EDITOR
			// use dynamic path to render selected indices
			if( bHasSelectedInstances )
			{
				Result.bDynamicRelevance = true;
			}
#endif
		}
		return Result;
	}

	/** Common path for the Get*MeshElement functions */
	inline void SetupInstancedMeshBatch(int32 LODIndex, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches) const;

	/** Draw the scene proxy as a dynamic element */
	virtual void DrawDynamicElements(FPrimitiveDrawInterface* PDI,const FSceneView* View) override;
		
	/** Sets up a shadow FMeshBatch for a specific LOD. */
	virtual bool GetShadowMeshElements(int32 LODIndex, uint8 InDepthPriorityGroup, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches) const override;

	/** Sets up a FMeshBatch for a specific LOD and element. */
	virtual bool GetMeshElements(int32 LODIndex,int32 ElementIndex,uint8 InDepthPriorityGroup, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches, const bool bUseSelectedMaterial, const bool bUseHoveredMaterial) const override;

	/** Sets up a wireframe FMeshBatch for a specific LOD. */
	virtual bool GetWireframeMeshElements(int32 LODIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches) const override;

	/**
	 * Creates the hit proxies are used when DrawDynamicElements is called.
	 * Called in the game thread.
	 * @param OutHitProxies - Hit proxes which are created should be added to this array.
	 * @return The hit proxy to use by default for elements drawn by DrawDynamicElements.
	 */
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component,TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override
	{
		if( InstancedRenderData.HitProxies.Num() )
		{
			// Add any per-instance hit proxies.
			OutHitProxies += InstancedRenderData.HitProxies;

			// No default hit proxy.
			return NULL;
		}
		else
		{
			return FStaticMeshSceneProxy::CreateHitProxies(Component, OutHitProxies);
		}
	}

	virtual bool IsDetailMesh() const override
	{
		return true;
	}

private:
	/** Per component render data */
	FInstancedStaticMeshRenderData InstancedRenderData;

#if WITH_EDITOR
	/* If we we have any selected instances */
	bool bHasSelectedInstances;
#else
	static const bool bHasSelectedInstances = false;
#endif

	/** LOD transition info. */
	FInstancingUserData UserData_AllInstances;
	FInstancingUserData UserData_SelectedInstances;
	FInstancingUserData UserData_DeselectedInstances;
};

void FInstancedStaticMeshSceneProxy::DrawDynamicElements(FPrimitiveDrawInterface* PDI,const FSceneView* View)
{
	QUICK_SCOPE_CYCLE_COUNTER( STAT_InstancedStaticMeshSceneProxy_DrawDynamicElements );

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const bool bSelectionRenderEnabled = GIsEditor && View->Family->EngineShowFlags.Selection;

	// If the first pass rendered selected instances only, we need to render the deselected instances in a second pass
	const int32 NumPasses = (bSelectionRenderEnabled && bHasSelectedInstances && !PDI->IsRenderingSelectionOutline()) ? 2 : 1;

	FInstancingUserData* PassUserData[2] =
	{
		bHasSelectedInstances && bSelectionRenderEnabled ? &UserData_SelectedInstances : &UserData_AllInstances,
		&UserData_DeselectedInstances
	};

	bool PassRenderSelection[2] = 
	{
		bSelectionRenderEnabled && IsSelected(),
		false
	};

	const FLinearColor UtilColor( LevelColor );
	const int32 LODsToDraw[] = { GetLOD(View) };
	const bool bIsWireframe = View->Family->EngineShowFlags.Wireframe;
	int32 NumLODs = StaticMesh->GetNumLODs();

	for( int32 Pass=0;Pass < NumPasses; Pass++ )
	{
		for (int32 LODLoopIndex = 0; LODLoopIndex < ARRAY_COUNT(LODsToDraw) && LODsToDraw[LODLoopIndex] != INDEX_NONE && LODsToDraw[LODLoopIndex] < NumLODs; LODLoopIndex++)
		{
			const int32 LODIndex = LODsToDraw[LODLoopIndex];

			const FStaticMeshLODResources& LODModel = StaticMesh->RenderData->LODResources[LODIndex];

			for(int32 SectionIndex = 0;SectionIndex < LODModel.Sections.Num();SectionIndex++)
			{
				TArray<FMeshBatch, TInlineAllocator<1>> MeshBatches;
				new (MeshBatches) FMeshBatch();
				if (GetMeshElements(LODIndex,SectionIndex,GetDepthPriorityGroup(View), MeshBatches, PassRenderSelection[Pass], IsHovered()))
				{
					for (int32 Index = 0; Index < MeshBatches.Num(); ++Index)
					{
						MeshBatches[Index].Elements[0].UserData = PassUserData[Pass];

						const int32 NumCalls = DrawRichMesh(
							PDI,
							MeshBatches[Index],
							WireframeColor,
							UtilColor,
							PropertyColor,
							this,
							PassRenderSelection[Pass],
							bIsWireframe
							);
						INC_DWORD_STAT_BY(STAT_StaticMeshTriangles,MeshBatches[Index].GetNumPrimitives() * NumCalls);
					}
				}
			}
		}
	}
#endif
}

inline void FInstancedStaticMeshSceneProxy::SetupInstancedMeshBatch(int32 LODIndex, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches) const
{
	const bool bInstanced = RHISupportsInstancing(GRHIShaderPlatform);
	OutMeshBatches[0].VertexFactory = &InstancedRenderData.VertexFactories[LODIndex];
	uint32 NumInstances = InstancedRenderData.InstanceBuffer.GetNumInstances();
	auto* OutBatchElement0 = OutMeshBatches[0].Elements.GetTypedData();
	OutBatchElement0->UserData = (void*)&UserData_AllInstances;
	OutBatchElement0->UserIndex = 0;
	if (bInstanced)
	{
		OutBatchElement0->NumInstances = NumInstances;
	}
	else
	{
		// Create a BatchElement per Instance, and if they overflow the size per mask, create additional MeshBatches

		const uint32 TotalInstances = NumInstances;
		const uint32 MaxInstancesPerBatch = FInstancedStaticMeshVertexFactory::NumBitsForVisibilityMask();
		uint32 NumBatches = (TotalInstances + MaxInstancesPerBatch - 1) / MaxInstancesPerBatch;

		// Make a copy of the Original MeshBatch, as it only has one Element at this point (cheaper to memcpy)
		FMeshBatch OriginalBatch = OutMeshBatches[0];

		// Add more Batches if we can't fit all Instances within the visibility mask
		OutMeshBatches.Reserve(NumBatches);

		uint32 RemainingInstances = TotalInstances;
		int32 InstanceIndex = 0;
		for (uint32 Batch = 0; Batch < NumBatches; ++Batch)
		{
			NumInstances = FMath::Min(RemainingInstances, MaxInstancesPerBatch);
			if (Batch > 0)
			{
				auto* NewBatch = new (OutMeshBatches) FMeshBatch();
				*NewBatch = OriginalBatch;
			}

			OutMeshBatches[Batch].Elements.Reserve(NumInstances);
			for (uint32 Instance = 0; Instance < NumInstances; ++Instance)
			{
				auto* NewBatchElement = (Instance == 0)
					? OutMeshBatches[Batch].Elements.GetTypedData()
					: new(OutMeshBatches[Batch].Elements) FMeshBatchElement();
				*NewBatchElement = *OutBatchElement0;
				NewBatchElement->UserIndex = InstanceIndex++;
			}

			RemainingInstances -= NumInstances;
		}

		check(RemainingInstances == 0);
	}
}


bool FInstancedStaticMeshSceneProxy::GetShadowMeshElements(int32 LODIndex, uint8 InDepthPriorityGroup, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches) const
{
	if (LODIndex < InstancedRenderData.VertexFactories.Num() && FStaticMeshSceneProxy::GetShadowMeshElements(LODIndex, InDepthPriorityGroup, OutMeshBatches))
	{
		SetupInstancedMeshBatch(LODIndex, OutMeshBatches);
		return true;
	}
	return false;
}

/** Sets up a FMeshBatch for a specific LOD and element. */
bool FInstancedStaticMeshSceneProxy::GetMeshElements(int32 LODIndex,int32 ElementIndex,uint8 InDepthPriorityGroup, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches, const bool bUseSelectedMaterial, const bool bUseHoveredMaterial) const
{
	if (LODIndex < InstancedRenderData.VertexFactories.Num() && FStaticMeshSceneProxy::GetMeshElements(LODIndex, ElementIndex, InDepthPriorityGroup, OutMeshBatches, bUseSelectedMaterial, bUseHoveredMaterial))
	{
		SetupInstancedMeshBatch(LODIndex, OutMeshBatches);
		return true;
	}
	return false;
};

/** Sets up a wireframe FMeshBatch for a specific LOD. */
bool FInstancedStaticMeshSceneProxy::GetWireframeMeshElements(int32 LODIndex, const FMaterialRenderProxy* WireframeRenderProxy, uint8 InDepthPriorityGroup, TArray<FMeshBatch, TInlineAllocator<1>>& OutMeshBatches) const
{
	if (LODIndex < InstancedRenderData.VertexFactories.Num() && FStaticMeshSceneProxy::GetWireframeMeshElements(LODIndex, WireframeRenderProxy, InDepthPriorityGroup, OutMeshBatches))
	{
		SetupInstancedMeshBatch(LODIndex, OutMeshBatches);
		return true;
	}
	return false;
}


/*-----------------------------------------------------------------------------
	UInstancedStaticMeshComponent
-----------------------------------------------------------------------------*/

UInstancedStaticMeshComponent::UInstancedStaticMeshComponent(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
	Mobility = EComponentMobility::Movable;
	BodyInstance.bSimulatePhysics = false;
}

#if WITH_EDITOR
/** Helper class used to preserve selection state across component duplication */
class FInstancedStaticMeshSelectionData : public FComponentInstanceDataBase
{
public:
	FInstancedStaticMeshSelectionData(const UInstancedStaticMeshComponent& InComponent)
		: FComponentInstanceDataBase(&InComponent)
		, SelectedInstances(InComponent.SelectedInstances)
	{
	}

public:
	/** The cached selected instances */
	TBitArray<> SelectedInstances;
};
#endif

FName UInstancedStaticMeshComponent::GetComponentInstanceDataType() const
{
	static const FName InstanceStaticMeshComponentInstanceDataName(TEXT("InstancedStaticMeshSelectionData"));
	return InstanceStaticMeshComponentInstanceDataName;
}

TSharedPtr<FComponentInstanceDataBase> UInstancedStaticMeshComponent::GetComponentInstanceData() const
{
	TSharedPtr<FComponentInstanceDataBase> InstanceData;
#if WITH_EDITOR
	InstanceData = MakeShareable(new FInstancedStaticMeshSelectionData(*this));
#endif
	return InstanceData;
}

void UInstancedStaticMeshComponent::ApplyComponentInstanceData(TSharedPtr<FComponentInstanceDataBase> ComponentInstanceData)
{
#if WITH_EDITOR
	check(ComponentInstanceData.IsValid());
	SelectedInstances = StaticCastSharedPtr<FInstancedStaticMeshSelectionData>(ComponentInstanceData)->SelectedInstances;
#endif
}

FPrimitiveSceneProxy* UInstancedStaticMeshComponent::CreateSceneProxy()
{
	// Verify that the mesh is valid before using it.
	const bool bMeshIsValid = 
		// make sure we have instances
		PerInstanceSMData.Num() > 0 &&
		// make sure we have an actual staticmesh
		StaticMesh &&
		StaticMesh->HasValidRenderData() &&
		// You really can't use hardware instancing on the consoles with multiple elements because they share the same index buffer. 
		// @todo: Level error or something to let LDs know this
		1;//StaticMesh->LODModels(0).Elements.Num() == 1;

	if(bMeshIsValid)
	{
		// If we don't have a random seed for this instanced static mesh component yet, then go ahead and
		// generate one now.  This will be saved with the static mesh component and used for future generation
		// of random numbers for this component's instances. (Used by the PerInstanceRandom material expression)
		while( InstancingRandomSeed == 0 )
		{
			InstancingRandomSeed = FMath::Rand();
		}

		return ::new FInstancedStaticMeshSceneProxy(this);
	}
	else
	{
		return NULL;
	}
}

void UInstancedStaticMeshComponent::InitInstanceBody(int32 InstanceIdx, FBodyInstance* InstanceBodyInstance)
{
	if (!StaticMesh)
	{
		UE_LOG(LogStaticMesh, Warning, TEXT("Unabled to create a body instance for %s in Actor %s. No StaticMesh set."), *GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("?"));
		return;
	}

	check(InstanceIdx < PerInstanceSMData.Num());
	check(InstanceIdx < InstanceBodies.Num());
	check(InstanceBodyInstance);

	UBodySetup* BodySetup = GetBodySetup();
	check(BodySetup);

	// Get transform of the instance
	FTransform InstanceTransform = FTransform(PerInstanceSMData[InstanceIdx].Transform) * ComponentToWorld;
	
	InstanceBodyInstance->CopyBodyInstancePropertiesFrom(&BodyInstance);
	InstanceBodyInstance->InstanceBodyIndex = InstanceIdx; // Set body index 

	// make sure we never enable bSimulatePhysics for ISMComps
	InstanceBodyInstance->bSimulatePhysics = false;

#if WITH_PHYSX
	// Create physics body instance.
	InstanceBodyInstance->InitBody( BodySetup, InstanceTransform, this, GetWorld()->GetPhysicsScene(), Aggregate);
#endif //WITH_PHYSX
}

void UInstancedStaticMeshComponent::CreateAllInstanceBodies()
{
	int32 NumBodies = PerInstanceSMData.Num();
	InstanceBodies.Init(NumBodies);

	for (int32 i = 0; i < NumBodies; ++i)
	{
		InstanceBodies[i] = new FBodyInstance;
		InitInstanceBody(i, InstanceBodies[i]);
	}
}

void UInstancedStaticMeshComponent::ClearAllInstanceBodies()
{
	for (int32 i = 0; i < InstanceBodies.Num(); i++)
	{
		check(InstanceBodies[i]);
		InstanceBodies[i]->TermBody();
		delete InstanceBodies[i];
	}

	InstanceBodies.Empty();
}


void UInstancedStaticMeshComponent::CreatePhysicsState()
{
	check(InstanceBodies.Num() == 0);

	FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();

	if (!PhysScene) { return; }

#if WITH_PHYSX
	check(Aggregate == NULL);
	Aggregate = GPhysXSDK->createAggregate(AggregateMaxSize, false);

	// Get the scene type from the main BodyInstance
	const uint32 SceneType = BodyInstance.UseAsyncScene() ? PST_Async : PST_Sync;
	PhysScene->GetPhysXScene(SceneType)->addAggregate(*Aggregate);
#endif

	// Create all the bodies.
	CreateAllInstanceBodies();

	USceneComponent::CreatePhysicsState();
}

void UInstancedStaticMeshComponent::DestroyPhysicsState()
{
	USceneComponent::DestroyPhysicsState();

	// Release all physics representations
	ClearAllInstanceBodies();

#if WITH_PHYSX
	// releasing Aggregate, it shouldn't contain any Bodies now, because they are released above
	if(Aggregate)
	{
		check(!Aggregate->getNbActors());
		Aggregate->release();
		Aggregate = NULL;
	}
#endif //WITH_PHYSX
}

bool UInstancedStaticMeshComponent::CanEditSimulatePhysics()
{
	// if instancedstaticmeshcomponent, we will never allow it
	return false;
}

FBoxSphereBounds UInstancedStaticMeshComponent::CalcBounds(const FTransform& BoundTransform) const
{
	FMatrix BoundTransformMatrix = BoundTransform.ToMatrixWithScale();

	if(StaticMesh && PerInstanceSMData.Num() > 0)
	{
		FBoxSphereBounds RenderBounds = StaticMesh->GetBounds();
		FBoxSphereBounds NewBounds = RenderBounds.TransformBy(PerInstanceSMData[0].Transform * BoundTransformMatrix);

		for (int32 InstanceIndex = 1; InstanceIndex < PerInstanceSMData.Num(); InstanceIndex++)
		{
			NewBounds = NewBounds + RenderBounds.TransformBy(PerInstanceSMData[InstanceIndex].Transform * BoundTransformMatrix);
		}

		return NewBounds;
	}
	else
	{
		return Super::CalcBounds(BoundTransform);
	}
}

#if WITH_EDITOR
/*-----------------------------------------------------------------------------
	FInstancedStaticMeshStaticLightingMesh
-----------------------------------------------------------------------------*/

/**
 * A static lighting mesh class that transforms the points by the per-instance transform of an 
 * InstancedStaticMeshComponent
 */
class FStaticLightingMesh_InstancedStaticMesh : public FStaticMeshStaticLightingMesh
{
public:

	/** Initialization constructor. */
	FStaticLightingMesh_InstancedStaticMesh(const UInstancedStaticMeshComponent* InPrimitive, int32 InstanceIndex, const TArray<ULightComponent*>& InRelevantLights)
		: FStaticMeshStaticLightingMesh(InPrimitive, 0, InRelevantLights)
	{
		// override the local to world to combine the per instance transform with the component's standard transform
		SetLocalToWorld(InPrimitive->PerInstanceSMData[InstanceIndex].Transform * InPrimitive->ComponentToWorld.ToMatrixWithScale());
	}
};

/*-----------------------------------------------------------------------------
	FInstancedStaticMeshStaticLightingTextureMapping
-----------------------------------------------------------------------------*/


/** Represents a static mesh primitive with texture mapped static lighting. */
class FStaticLightingTextureMapping_InstancedStaticMesh : public FStaticMeshStaticLightingTextureMapping
{
public:
	/** Initialization constructor. */
	FStaticLightingTextureMapping_InstancedStaticMesh(UInstancedStaticMeshComponent* InPrimitive, FStaticLightingMesh* InMesh)
		: FStaticMeshStaticLightingTextureMapping(InPrimitive, 0, InMesh, 0, 0, 0, false)
	{
		// We don't actually support light/shadow mapping on instanced meshes, only *casting* shadows
		bProcessMapping = false;
	}

	virtual void Apply(FQuantizedLightmapData* QuantizedData, const TMap<ULightComponent*, FShadowMapData2D*>& ShadowMapData) override
	{
		// Not supported
	}

#if WITH_EDITOR
	virtual bool DebugThisMapping() const override
	{
		return false;
	}
#endif	//WITH_EDITOR

	virtual FString GetDescription() const override
	{
		return FString(TEXT("InstancedSMLightingMapping"));
	}
};

void UInstancedStaticMeshComponent::GetStaticLightingInfo(FStaticLightingPrimitiveInfo& OutPrimitiveInfo,const TArray<ULightComponent*>& InRelevantLights,const FLightingBuildOptions& Options)
{
	// We don't support light/shadow mapping for instanced meshes, only *casting* shadows
	// we intentionally ignore the mobility setting here, as foliage is marked "Movable" to force
	// dynamic lighting due to static lighting not being supported, but is actually static
	if (StaticMesh && bCastStaticShadow)
	{
		for (int32 InstanceIndex = 0; InstanceIndex < PerInstanceSMData.Num(); InstanceIndex++)
		{
			FStaticLightingMesh_InstancedStaticMesh* StaticLightingMesh = new FStaticLightingMesh_InstancedStaticMesh(this, InstanceIndex, InRelevantLights);
			OutPrimitiveInfo.Meshes.Add(StaticLightingMesh);

			FStaticLightingTextureMapping_InstancedStaticMesh* InstancedMapping = new FStaticLightingTextureMapping_InstancedStaticMesh(this, StaticLightingMesh);
			OutPrimitiveInfo.Mappings.Add(InstancedMapping);
		}
	}
}
#endif

/**
 * Structure that maps a component to it's lighting/instancing specific data which must be the same
 * between all instances that are bound to that component.
 */
struct FComponentInstanceSharingData
{
	/** The component that is associated (owns) this data */
	UInstancedStaticMeshComponent* Component;

	/** Light map texture */
	UTexture* LightMapTexture;

	/** Shadow map texture (or NULL if no shadow map) */
	UTexture* ShadowMapTexture;


	FComponentInstanceSharingData()
		: Component( NULL ),
		  LightMapTexture( NULL ),
		  ShadowMapTexture( NULL )
	{
	}
};


/**
 * Helper struct to hold information about what components use what lightmap textures
 */
struct FComponentInstancedLightmapData
{
	/** List of all original components and their original instances containing */
	TMap<UInstancedStaticMeshComponent*, TArray<FInstancedStaticMeshInstanceData> > ComponentInstances;

	/** List of new components */
	TArray< FComponentInstanceSharingData > SharingData;
};

/**
 * Struct that controls what we use to determine compatible components
 */
struct FValidCombination
{
	/** An optional key for marking components as compatible (eg proc buildings only allow meshes on a single face to join) */
	int32 JoinKey;

	/** Different meshes are never compatible */
	UStaticMesh* Mesh;

	friend bool operator==(const FValidCombination& A, const FValidCombination& B)
	{
		return A.JoinKey == B.JoinKey && A.Mesh == B.Mesh;
	}

	friend uint32 GetTypeHash(const FValidCombination& Combo)
	{
		return (uint32)(PTRINT)Combo.Mesh * Combo.JoinKey;
	}
};

void UInstancedStaticMeshComponent::GetLightAndShadowMapMemoryUsage( int32& LightMapMemoryUsage, int32& ShadowMapMemoryUsage ) const
{
	Super::GetLightAndShadowMapMemoryUsage(LightMapMemoryUsage, ShadowMapMemoryUsage);

	int32 NumInstances = PerInstanceSMData.Num();

	// Scale lighting demo by number of instances
	LightMapMemoryUsage *= NumInstances;
	ShadowMapMemoryUsage *= NumInstances;
}


void UInstancedStaticMeshComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	PerInstanceSMData.BulkSerialize(Ar);

#if WITH_EDITOR
	if( Ar.IsTransacting() )
	{
		Ar << SelectedInstances;
	}
#endif
}

void UInstancedStaticMeshComponent::AddInstance(const FTransform& InstanceTransform)
{
	FInstancedStaticMeshInstanceData* NewInstanceData = new(PerInstanceSMData) FInstancedStaticMeshInstanceData();
	SetupNewInstanceData(*NewInstanceData, PerInstanceSMData.Num() - 1, InstanceTransform);

	// added with a set transform, don't allow instance selection to manipulate the transform in the editor
	bHasPerInstanceHitProxies = false;

	MarkRenderStateDirty();

	if (IsCollisionEnabled() && GetWorld()->GetNavigationSystem() != nullptr)
	{
		GetWorld()->GetNavigationSystem()->UpdateNavOctree(this);
	}
}

void UInstancedStaticMeshComponent::AddInstanceWorldSpace(const FTransform& WorldTransform)
 {
	// Transform from world space to local space
	FTransform RelativeTM = WorldTransform.GetRelativeTransform(ComponentToWorld);
	AddInstance(RelativeTM);
}

bool UInstancedStaticMeshComponent::GetInstanceTransform(int32 InstanceIndex, FTransform& OutInstanceTransform, bool bWorldSpace) const
{
	if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	const FInstancedStaticMeshInstanceData& InstanceData = PerInstanceSMData[InstanceIndex];

	OutInstanceTransform = FTransform(InstanceData.Transform);
	if (bWorldSpace)
	{
		OutInstanceTransform = OutInstanceTransform * ComponentToWorld;
	}

	return true;
}

bool UInstancedStaticMeshComponent::UpdateInstanceTransform(int32 InstanceIndex, const FTransform& NewInstanceTransform, bool bWorldSpace)
{
	if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	FInstancedStaticMeshInstanceData& InstanceData = PerInstanceSMData[InstanceIndex];

	// Render data uses local transform of the instance
	FTransform LocalTransform = bWorldSpace ? NewInstanceTransform.GetRelativeTransform(ComponentToWorld) : NewInstanceTransform;
	InstanceData.Transform = LocalTransform.ToMatrixWithScale();

	if (bPhysicsStateCreated)
	{
		// Physics uses world transform of the instance
		FTransform WorldTransform = bWorldSpace ? NewInstanceTransform : (LocalTransform * ComponentToWorld);
		FBodyInstance* InstanceBodyInstance = InstanceBodies[InstanceIndex];
#if WITH_PHYSX
		// Update transform.
		InstanceBodyInstance->SetBodyTransform(WorldTransform, false);
#endif //WITH_PHYSX
	}

	MarkRenderStateDirty();

	return true;
}

bool UInstancedStaticMeshComponent::ShouldCreatePhysicsState() const
{
	return IsRegistered() && (bAlwaysCreatePhysicsState || IsCollisionEnabled());
}

bool UInstancedStaticMeshComponent::RemoveInstance(int32 InstanceIndex)
{
	if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
	{
		return false;
	}

	// remove instance
	PerInstanceSMData.RemoveAt(InstanceIndex);

#if WITH_EDITOR
	// remove selection flag if array is filled in
	if (SelectedInstances.IsValidIndex(InstanceIndex))
	{
		SelectedInstances.RemoveAt(InstanceIndex);
	}
#endif

	// update the physics state
	if (bPhysicsStateCreated)
	{
		// TODO: it may be possible to instead just update the BodyInstanceIndex for all bodies after the removed instance. 
		ClearAllInstanceBodies();
		CreateAllInstanceBodies();
	}

	// Indicate we need to update render state to reflect changes
	MarkRenderStateDirty();

	if (IsCollisionEnabled() && GetWorld()->GetNavigationSystem() != nullptr)
	{
		GetWorld()->GetNavigationSystem()->UpdateNavOctree(this);
	}

	return true;
}

void UInstancedStaticMeshComponent::ClearInstances()
{
	// Clear all the per-instance data
	PerInstanceSMData.Empty();
	// Release any physics representations
	ClearAllInstanceBodies();

	// Indicate we need to update render state to reflect changes
	MarkRenderStateDirty();

	if (IsCollisionEnabled() && GetWorld()->GetNavigationSystem() != nullptr)
	{
		GetWorld()->GetNavigationSystem()->UpdateNavOctree(this);
	}
}

int32 UInstancedStaticMeshComponent::GetInstanceCount() const
{
	return PerInstanceSMData.Num();
}

void UInstancedStaticMeshComponent::SetCullDistances(int32 StartCullDistance, int32 EndCullDistance)
{
	InstanceStartCullDistance = StartCullDistance;
	InstanceEndCullDistance = EndCullDistance;
	MarkRenderStateDirty();
}

void UInstancedStaticMeshComponent::SetupNewInstanceData(FInstancedStaticMeshInstanceData& InOutNewInstanceData, int32 InInstanceIndex, const FTransform& InInstanceTransform)
{
	InOutNewInstanceData.Transform = InInstanceTransform.ToMatrixWithScale();
	InOutNewInstanceData.LightmapUVBias = FVector2D( -1.0f, -1.0f );
	InOutNewInstanceData.ShadowmapUVBias = FVector2D( -1.0f, -1.0f );

	if (bPhysicsStateCreated)
	{
		FBodyInstance* NewBodyInstance = new FBodyInstance();
		int32 BodyIndex = InstanceBodies.Insert(NewBodyInstance, InInstanceIndex);

		check(InInstanceIndex == BodyIndex);
		InitInstanceBody(BodyIndex, NewBodyInstance);
	}
}

bool UInstancedStaticMeshComponent::DoCustomNavigableGeometryExport(struct FNavigableGeometryExport* GeomExport) const
{
	if (StaticMesh != NULL)
	{
		UNavCollision* NavCollision = StaticMesh->NavCollision;
		if (NavCollision != NULL && NavCollision->bHasConvexGeometry)
		{
			for (const FInstancedStaticMeshInstanceData& InstanceData : PerInstanceSMData)
			{
				const FVector Scale3D = InstanceData.Transform.GetScaleVector();
				// if any of scales is 0 there's no point in exporting it
				if (!Scale3D.IsZero())
				{
					GeomExport->ExportCustomMesh(NavCollision->ConvexCollision.VertexBuffer.GetData(), NavCollision->ConvexCollision.VertexBuffer.Num(),
						NavCollision->ConvexCollision.IndexBuffer.GetData(), NavCollision->ConvexCollision.IndexBuffer.Num(), FTransform(InstanceData.Transform) * ComponentToWorld);

					GeomExport->ExportCustomMesh(NavCollision->TriMeshCollision.VertexBuffer.GetData(), NavCollision->TriMeshCollision.VertexBuffer.Num(),
						NavCollision->TriMeshCollision.IndexBuffer.GetData(), NavCollision->TriMeshCollision.IndexBuffer.Num(), FTransform(InstanceData.Transform) * ComponentToWorld);
				}
			}
		}
		else
		{
			UBodySetup* BodySetup = StaticMesh->BodySetup;
			if (BodySetup)
			{
				for (const FInstancedStaticMeshInstanceData& InstanceData : PerInstanceSMData)
				{
					const FVector Scale3D = InstanceData.Transform.GetScaleVector();
					// if any of scales is 0 there's no point in exporting it
					if (!Scale3D.IsZero())
					{
						GeomExport->ExportRigidBodySetup(*BodySetup, FTransform(InstanceData.Transform) * ComponentToWorld);
					}
				}

				//GeomExport->SlopeOverride = BodySetup->WalkableSlopeOverride;
			}
		}
	}

	// we don't want "regular" collision export for this component
	return false;
}

SIZE_T UInstancedStaticMeshComponent::GetResourceSize( EResourceSizeMode::Type Mode )
{
	SIZE_T ResSize = 0;

	for (int32 i=0; i < InstanceBodies.Num(); ++i)
	{
		if (InstanceBodies[i] != NULL && InstanceBodies[i]->IsValidBodyInstance())
		{
			ResSize += InstanceBodies[i]->GetBodyInstanceResourceSize(Mode);
		}
	}

	return ResSize;
}

#if WITH_EDITOR
void UInstancedStaticMeshComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if(PropertyChangedEvent.Property != NULL && PropertyChangedEvent.Property->GetFName() == "PerInstanceSMData")
	{
		if(PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayAdd)
		{
			int32 AddedAtIndex = PropertyChangedEvent.GetArrayIndex(PropertyChangedEvent.Property->GetFName().ToString());
			check(AddedAtIndex != INDEX_NONE);
			SetupNewInstanceData(PerInstanceSMData[AddedAtIndex], AddedAtIndex, FTransform::Identity);

			// added via the property editor, so we will want to interactively work with instances
			bHasPerInstanceHitProxies = true;
		}

		MarkRenderStateDirty();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif

bool UInstancedStaticMeshComponent::IsInstanceSelected(int32 InInstanceIndex) const
{
#if WITH_EDITOR
	if(SelectedInstances.IsValidIndex(InInstanceIndex))
	{
		return SelectedInstances[InInstanceIndex];
	}
#endif

	return false;
}

void UInstancedStaticMeshComponent::SelectInstance(bool bInSelected, int32 InInstanceIndex, int32 InInstanceCount)
{
#if WITH_EDITOR
	if (bHasPerInstanceHitProxies)
	{
		if(PerInstanceSMData.Num() != SelectedInstances.Num())
		{
			SelectedInstances.Init(false, PerInstanceSMData.Num());
		}

		check(SelectedInstances.IsValidIndex(InInstanceIndex));
		check(SelectedInstances.IsValidIndex(InInstanceIndex + (InInstanceCount - 1)));

		for (int32 InstanceIndex = InInstanceIndex; InstanceIndex < InInstanceIndex + InInstanceCount; InstanceIndex++)
		{
			SelectedInstances[InstanceIndex] = bInSelected;
		}
	}
#endif
}

void FInstancedStaticMeshVertexFactoryShaderParameters::SetMesh( FRHICommandList& RHICmdList, FShader* VertexShader,const class FVertexFactory* VertexFactory,const class FSceneView& View,const struct FMeshBatchElement& BatchElement,uint32 DataFlags ) const 
{
	FLocalVertexFactoryShaderParameters::SetMesh(RHICmdList, VertexShader, VertexFactory, View, BatchElement, DataFlags);

	FRHIVertexShader* VS = VertexShader->GetVertexShader();
	if( InstancingFadeOutParamsParameter.IsBound() )
	{
		FVector4 InstancingFadeOutParams(0.f,0.f,1.f,1.f);

		const FInstancingUserData* InstancingUserData = (FInstancingUserData*)BatchElement.UserData;
		if (InstancingUserData)
		{
			InstancingFadeOutParams.X = InstancingUserData->StartCullDistance;
			if( InstancingUserData->EndCullDistance > 0 )
			{
				if( InstancingUserData->EndCullDistance > InstancingUserData->StartCullDistance )
				{
					InstancingFadeOutParams.Y = 1.f / (float)(InstancingUserData->EndCullDistance - InstancingUserData->StartCullDistance);
				}
				else
				{
					InstancingFadeOutParams.Y = 1.f;
				}
			}
			else
			{
				InstancingFadeOutParams.Y = 0.f;
			}

			InstancingFadeOutParams.Z = InstancingUserData->bRenderSelected ? 1.f : 0.f;
			InstancingFadeOutParams.W = InstancingUserData->bRenderUnselected ? 1.f : 0.f;
		}
		SetShaderValue(RHICmdList, VS, InstancingFadeOutParamsParameter, InstancingFadeOutParams );
	}

	const bool bInstanced = RHISupportsInstancing(GRHIShaderPlatform);
	if (!bInstanced)
	{
		if (CPUInstanceShadowMapBias.IsBound())
		{
			const auto* InstancingData = (FInstancingUserData*)BatchElement.UserData;
			auto* InstanceStream = ((FInstancingUserData::FInstanceStream*)InstancingData->RenderData->InstanceBuffer.GetRawData()) + BatchElement.UserIndex;
			SetShaderValue(RHICmdList, VS, CPUInstanceShadowMapBias, InstanceStream->InstanceShadowmapUVBias);
			SetShaderValueArray(RHICmdList, VS, CPUInstanceTransform, InstanceStream->InstanceTransform, 3);
			SetShaderValueArray(RHICmdList, VS, CPUInstanceInverseTransform, InstanceStream->InstanceInverseTransform, 3);
		}
	}
}

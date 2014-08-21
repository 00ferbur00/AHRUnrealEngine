// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SceneRenderTargets.cpp: Scene render target implementation.
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneFilterRendering.h"
#include "ReflectionEnvironment.h"
#include "LightPropagationVolume.h"
#include "SceneUtils.h"

// for LightPropagationVolume feature, could be exposed
const int ReflectiveShadowMapResolution = 256;

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FGBufferResourceStruct,TEXT("GBuffers"));

/*-----------------------------------------------------------------------------
FSceneRenderTargets
-----------------------------------------------------------------------------*/

int32 GDownsampledOcclusionQueries = 0;
static FAutoConsoleVariableRef CVarDownsampledOcclusionQueries(
	TEXT("r.DownsampledOcclusionQueries"),
	GDownsampledOcclusionQueries,
	TEXT("Whether to issue occlusion queries to a downsampled depth buffer"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarSceneTargetsResizingMethod(
	TEXT("r.SceneRenderTargetResizeMethod"),
	0,
	TEXT("Control the scene render target resize method:\n")
	TEXT("(This value is only used in game mode and on windowing platforms.)\n")
	TEXT("0: Resize to match requested render size (Default) (Least memory use, can cause stalls when size changes e.g. ScreenPercentage)\n")
	TEXT("1: Fixed to screen resolution.\n")
	TEXT("2: Expands to encompass the largest requested render dimension. (Most memory use, least prone to allocation stalls.)"),	
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarOptimizeForUAVPerformance(
	TEXT("r.OptimizeForUAVPerformance"),
	0,
	TEXT("Allows to profile if hardware has a performance cost due to render target reuse (more info: search for GCNPerformanceTweets.pdf Tip 37)\n")
	TEXT("If we see a noticeable difference on some hardware we can add another option like -1 (meaning auto) and make it the new default.\n")
	TEXT("0: Optimize for GPU memory savings and reuse render targets (default)\n")
	TEXT("1: Optimize for GPU performance (might render faster but can require more GPU memory)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarCustomDepth(
	TEXT("r.CustomDepth"),
	1,
	TEXT("0: feature is disabled\n")
	TEXT("1: feature is enabled, texture is created on demand\n")
	TEXT("2: feature is enabled, texture is not released until required (should be the project setting if the feature should not stall)"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarMobileMSAA(
	TEXT("r.MobileMSAA"),
	0,
	TEXT("Use MSAA instead of Temporal AA on mobile:\n")
    TEXT("1: Use Temporal AA (MSAA disabled)\n")
    TEXT("2: Use 2x MSAA (Temporal AA disabled)\n")
    TEXT("4: Use 4x MSAA (Temporal AA disabled)\n"),
	ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<int32> CVarGBufferFormat(
	TEXT("r.GBufferFormat"),
	1,
	TEXT("Defines the memory layout used for the GBuffer.\n")
	TEXT("(affects performance, mostly through bandwidth, quality of normals and material attributes).\n")
	TEXT(" 0: lower precision (8bit per component, for profiling)\n")
	TEXT(" 1: low precision (default)\n")
	TEXT(" 5: high precision"),
	ECVF_RenderThreadSafe);

/** The global render targets used for scene rendering. */
TGlobalResource<FSceneRenderTargets> GSceneRenderTargets;

FIntPoint FSceneRenderTargets::GetSceneRenderTargetSize(const FSceneViewFamily& ViewFamily) const
{
	// Don't expose Clamped to the cvar since you need to at least grow to the initial state.
	enum ESizingMethods { RequestedSize, ScreenRes, Grow, VisibleSizingMethodsCount, Clamped};
	ESizingMethods SceneTargetsSizingMethod = Grow;

	bool bSceneCapture = false;
	for (int32 ViewIndex = 0; ViewIndex < ViewFamily.Views.Num(); ++ViewIndex)
	{
		bSceneCapture |= ViewFamily.Views[ViewIndex]->bIsSceneCapture;
	}

	if(!FPlatformProperties::SupportsWindowedMode())
	{
		// Force ScreenRes on non windowed platforms.
		SceneTargetsSizingMethod = RequestedSize;
	}
	else if (GIsEditor)
	{
		// Always grow scene render targets in the editor.
		SceneTargetsSizingMethod = Grow;
	}	
	else
	{
		// Otherwise use the setting specified by the console variable.
		SceneTargetsSizingMethod = (ESizingMethods) FMath::Clamp(CVarSceneTargetsResizingMethod.GetValueOnRenderThread(), 0, (int32)VisibleSizingMethodsCount);
	}
	
	if (bSceneCapture)
	{
		// In general, we don't want scenecapture to grow our buffers, because depending on the cvar for our game, we may not recover that memory.  This can be changed if necessary.
		// However, in the editor a user might have a small editor window, but be capturing cubemaps or other dynamic assets for data distribution, 
		// in which case we need to grow for correctness.
		// We also don't want to reallocate all our buffers for a temporary use case like a capture.  So we just clamp the biggest capture size to the currently available buffers.
		if (GIsEditor)
		{
			SceneTargetsSizingMethod = Grow;
		}
		else
		{			
			SceneTargetsSizingMethod = Clamped;
		}
	}

	switch (SceneTargetsSizingMethod)
	{
		case RequestedSize:
			return FIntPoint(ViewFamily.FamilySizeX, ViewFamily.FamilySizeY);
		case ScreenRes:
			return FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY);
		case Grow:
			return FIntPoint(FMath::Max((uint32)GetBufferSizeXY().X, ViewFamily.FamilySizeX),
					FMath::Max((uint32)GetBufferSizeXY().Y, ViewFamily.FamilySizeY));
		case Clamped:
			if (((uint32)BufferSize.X < ViewFamily.FamilySizeX) || ((uint32)BufferSize.Y < ViewFamily.FamilySizeY))
			{
				UE_LOG(LogRenderer, Warning, TEXT("Capture target size: %ux%u clamped to %ux%u."), ViewFamily.FamilySizeX, ViewFamily.FamilySizeY, BufferSize.X, BufferSize.Y);
			}
			return FIntPoint(GetBufferSizeXY().X, GetBufferSizeXY().Y);
		default:
			checkNoEntry();
			return FIntPoint::ZeroValue;
	}
}

void FSceneRenderTargets::Allocate(const FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	// If feature level has changed, release all previously allocated targets to the pool. If feature level has changed but
	const auto NewFeatureLevel = ViewFamily.Scene->GetFeatureLevel();

	FIntPoint DesiredBufferSize = GetSceneRenderTargetSize(ViewFamily);
	check(DesiredBufferSize.X > 0 && DesiredBufferSize.Y > 0);
	QuantizeBufferSize(DesiredBufferSize.X, DesiredBufferSize.Y);

	int GBufferFormat = CVarGBufferFormat.GetValueOnRenderThread();

	int SceneColorFormat;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SceneColorFormat"));

		SceneColorFormat = CVar->GetValueOnRenderThread();
	}
		
	bool bNewAllowStaticLighting;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));

		bNewAllowStaticLighting = CVar->GetValueOnRenderThread() != 0;
	}

	bool bDownsampledOcclusionQueries = GDownsampledOcclusionQueries != 0;

	int32 MaxShadowResolution = GetCachedScalabilityCVars().MaxShadowResolution;

	int32 TranslucencyLightingVolumeDim = GTranslucencyLightingVolumeDim;

	uint32 Mobile32bpp = !IsMobileHDR() || IsMobileHDR32bpp();

	static const auto CVarMobileMSAA = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MobileMSAA"));
	int32 MobileMSAA = GShaderPlatformForFeatureLevel[NewFeatureLevel] == SP_OPENGL_ES2_IOS ? 1 : CVarMobileMSAA->GetValueOnRenderThread();

	bool bLightPropagationVolume = UseLightPropagationVolumeRT(NewFeatureLevel);

	uint32 MinShadowResolution;
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));

		MinShadowResolution = CVar->GetValueOnRenderThread();
	}

	if( (BufferSize.X != DesiredBufferSize.X) ||
		(BufferSize.Y != DesiredBufferSize.Y) ||
		(CurrentGBufferFormat != GBufferFormat) ||
		(CurrentSceneColorFormat != SceneColorFormat) ||
		(bAllowStaticLighting != bNewAllowStaticLighting) ||
		(bUseDownsizedOcclusionQueries != bDownsampledOcclusionQueries) ||
 		(CurrentMaxShadowResolution != MaxShadowResolution) ||
		(CurrentTranslucencyLightingVolumeDim != TranslucencyLightingVolumeDim) ||
		(CurrentMobile32bpp != Mobile32bpp) ||
		(CurrentMobileMSAA != MobileMSAA) ||
		(bCurrentLightPropagationVolume != bLightPropagationVolume) ||
		(CurrentMinShadowResolution != MinShadowResolution) ||
		(NewFeatureLevel != CurrentFeatureLevel))
	{
		CurrentGBufferFormat = GBufferFormat;
		CurrentSceneColorFormat = SceneColorFormat;
		bAllowStaticLighting = bNewAllowStaticLighting;
		bUseDownsizedOcclusionQueries = bDownsampledOcclusionQueries;
		CurrentMaxShadowResolution = MaxShadowResolution;
		CurrentTranslucencyLightingVolumeDim = TranslucencyLightingVolumeDim;
		CurrentMobile32bpp = Mobile32bpp;
		CurrentMobileMSAA = MobileMSAA;
		CurrentMinShadowResolution = MinShadowResolution;
		bCurrentLightPropagationVolume = bLightPropagationVolume;
		CurrentFeatureLevel = NewFeatureLevel;
		
		// Reinitialize the render targets for the given size.
		SetBufferSize(DesiredBufferSize.X, DesiredBufferSize.Y);

		UE_LOG(LogRenderer, Warning, TEXT("Reallocating scene render targets to support %ux%u."), BufferSize.X, BufferSize.Y);

		UpdateRHI();
	}
}

/** Clears the GBuffer render targets to default values. */
void FSceneRenderTargets::ClearGBufferTargets(FRHICommandListImmediate& RHICmdList, const FLinearColor& ClearColor)
{
	SCOPED_DRAW_EVENT(ClearGBufferTargets, DEC_SCENE_ITEMS);

	// Clear GBufferA, GBufferB, GBufferC, GBufferD, GBufferE
	{
		GSceneRenderTargets.BeginRenderingSceneColor(RHICmdList, true);

		int32 NumToClear = GSceneRenderTargets.GetNumGBufferTargets();
		if (NumToClear > 1)
		{
			// Using 0 and 1 ensures we go through the fast path on Intel integrated GPUs.
			// Normal could be 0.5,0.5,0.5 but then it would not use the fast path.
			FLinearColor ClearColors[6] = {ClearColor, FLinearColor(0, 0, 0, 0), FLinearColor(0,0,0,0), FLinearColor(0,0,0,0), FLinearColor(0,1,1,1), FLinearColor(1,1,1,1)};
			RHICmdList.ClearMRT(true, NumToClear, ClearColors, false, 0, false, 0, FIntRect());
		}
		else
		{
			RHICmdList.Clear(true, ClearColor, false, 0, false, 0, FIntRect());
		}
	}
}

void FSceneRenderTargets::BeginRenderingSceneColor(FRHICommandList& RHICmdList, bool bGBufferPass)
{
	SCOPED_DRAW_EVENT(BeginRenderingSceneColor, DEC_SCENE_ITEMS);

	if(IsSimpleDynamicLightingEnabled())
	{
		bGBufferPass = false;
	}
	
	AllocSceneColor();
	
	// Set the scene color surface as the render target, and the scene depth surface as the depth-stencil target.
	if (bGBufferPass && CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		FTextureRHIParamRef RenderTargets[6] = {0};
		RenderTargets[0] = GetSceneColorSurface();
		RenderTargets[1] = GSceneRenderTargets.GBufferA->GetRenderTargetItem().TargetableTexture;
		RenderTargets[2] = GSceneRenderTargets.GBufferB->GetRenderTargetItem().TargetableTexture;
		RenderTargets[3] = GSceneRenderTargets.GBufferC->GetRenderTargetItem().TargetableTexture;
		RenderTargets[4] = GSceneRenderTargets.GBufferD->GetRenderTargetItem().TargetableTexture;

		uint32 MRTCount = ARRAY_COUNT(RenderTargets);

		if (bAllowStaticLighting)
		{
			RenderTargets[5] = GSceneRenderTargets.GBufferE->GetRenderTargetItem().TargetableTexture;
		}
		else
		{
			MRTCount--;
		}
		
		SetRenderTargets(RHICmdList, MRTCount, RenderTargets, GetSceneDepthSurface(), 0, NULL);
	}
	else
	{
		SetRenderTarget(RHICmdList, GetSceneColorSurface(), GetSceneDepthSurface());
	}
} 

int32 FSceneRenderTargets::GetNumGBufferTargets() const
{
	int32 NumGBufferTargets = 1;

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4 && !IsSimpleDynamicLightingEnabled())
	{
		NumGBufferTargets = bAllowStaticLighting ? 6 : 5;
	}
	return NumGBufferTargets;
}

void FSceneRenderTargets::AllocSceneColor()
{
	if(SceneColor)
	{
		// no work needed
		return;
	}

	// create SceneColor on demand so it can be shared with other pooled RT

	EPixelFormat SceneColorBufferFormat = GetSceneColorFormat();

	// Create the scene color.
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, SceneColorBufferFormat, TexCreate_None, TexCreate_RenderTargetable, false));

		Desc.Flags |= TexCreate_FastVRAM;

		int32 OptimizeForUAVPerformance = CVarOptimizeForUAVPerformance.GetValueOnRenderThread();

		// with TexCreate_UAV it would allow better sharing with later elements but it might come at a high cost:
		// GCNPerformanceTweets.pdf Tip 37: Warning: Causes additional synchronization between draw calls when using a render target allocated with this flag, use sparingly
		if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5 && !OptimizeForUAVPerformance)
		{
			Desc.TargetableFlags |= TexCreate_UAV;
		}

		if (CurrentFeatureLevel == ERHIFeatureLevel::ES2)
		{
		    uint16 NumSamples = GRHIShaderPlatform == SP_OPENGL_ES2_IOS ? 1 : CVarMobileMSAA.GetValueOnRenderThread();
	        if (NumSamples != 1 && NumSamples != 2 && NumSamples != 4)
			{
	            NumSamples = 1;
			}
			Desc.NumSamples = NumSamples;
		}

		GRenderTargetPool.FindFreeElement(Desc, SceneColor, TEXT("SceneColor"));
	}

	// otherwise we have a severe problem
	check(SceneColor);
}

void FSceneRenderTargets::AllocLightAttenuation()
{
	if(LightAttenuation)
	{
		// no work needed
		return;
	}

	check(IsInRenderingThread());

	// create LightAttenuation on demand so it can be shared with other pooled RT

	// Create a texture to store the resolved light attenuation values, and a render-targetable surface to hold the unresolved light attenuation values.
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, TexCreate_None, TexCreate_RenderTargetable, false));
		Desc.Flags |= TexCreate_FastVRAM;
		GRenderTargetPool.FindFreeElement(Desc, LightAttenuation, TEXT("LightAttenuation"));

		// the channel assignment is documented in ShadowRendering.cpp (look for Light Attenuation channel assignment)
	}

	// otherwise we have a severe problem
	check(LightAttenuation);
}

void FSceneRenderTargets::ReleaseGBufferTargets()
{
	GBufferResourcesUniformBuffer.SafeRelease();
	GBufferA.SafeRelease();
	GBufferB.SafeRelease();
	GBufferC.SafeRelease();
	GBufferD.SafeRelease();
	GBufferE.SafeRelease();
}

void FSceneRenderTargets::AllocGBufferTargets()
{
	// AdjustGBufferRefCount +1 doesn't match -1 (within the same frame)
	ensure(GBufferRefCount == 0);

	if(GBufferA)
	{
		// no work needed
		return;
	}

	// create GBuffer on demand so it can be shared with other pooled RT

	// good to see the quality loss due to precision in the gbuffer
	const bool bHighPrecisionGBuffers = (CurrentGBufferFormat >= 5);
	// good to profile the impact of non 8 bit formats
	const bool bEnforce8BitPerChannel = (CurrentGBufferFormat == 0);

	// Create the world-space normal g-buffer.
	{
		EPixelFormat NormalGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_A2B10G10R10;

		if(bEnforce8BitPerChannel)
		{
			NormalGBufferFormat = PF_B8G8R8A8;
		}

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, NormalGBufferFormat, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, GBufferA, TEXT("GBufferA"));
	}

	// Create the specular color and power g-buffer.
	{
		const EPixelFormat SpecularGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, SpecularGBufferFormat, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, GBufferB, TEXT("GBufferB"));
	}

	// Create the diffuse color g-buffer.
	{
		const EPixelFormat DiffuseGBufferFormat = bHighPrecisionGBuffers ? PF_FloatRGBA : PF_B8G8R8A8;
		uint32 DiffuseGBufferFlags = TexCreate_SRGB;

#if PLATFORM_MAC // @todo: remove once Apple fixes radr://16754329 AMD Cards don't always perform FRAMEBUFFER_SRGB if the draw FBO has mixed sRGB & non-SRGB colour attachments
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mac.UseFrameBufferSRGB"));
		DiffuseGBufferFlags = CVar && CVar->GetValueOnRenderThread() ? TexCreate_SRGB : TexCreate_None;
#endif

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, DiffuseGBufferFormat, DiffuseGBufferFlags, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, GBufferC, TEXT("GBufferC"));
	}

	// Create the mask g-buffer (e.g. SSAO, subsurface scattering, wet surface mask, skylight mask, ...).
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, GBufferD, TEXT("GBufferD"));
	}

	if (bAllowStaticLighting)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_B8G8R8A8, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, GBufferE, TEXT("GBufferE"));
	}

	// otherwise we have a severe problem
	check(GBufferA);

	// Create the required render targets if running Highend.
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{

		// Allocate the Gbuffer resource uniform buffer.
		const FSceneRenderTargetItem& GBufferA = this->GBufferA ? this->GBufferA->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferB = this->GBufferB ? this->GBufferB->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferC = this->GBufferC ? this->GBufferC->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferD = this->GBufferD ? this->GBufferD->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();
		const FSceneRenderTargetItem& GBufferE = this->GBufferE ? this->GBufferE->GetRenderTargetItem() : GSystemTextures.BlackDummy->GetRenderTargetItem();

		FGBufferResourceStruct GBufferResourceStruct;

		GBufferResourceStruct.GBufferATexture = GBufferA.ShaderResourceTexture;
		GBufferResourceStruct.GBufferBTexture = GBufferB.ShaderResourceTexture;
		GBufferResourceStruct.GBufferCTexture = GBufferC.ShaderResourceTexture;
		GBufferResourceStruct.GBufferDTexture = GBufferD.ShaderResourceTexture;
		GBufferResourceStruct.GBufferETexture = GBufferE.ShaderResourceTexture;

		GBufferResourceStruct.GBufferATextureNonMS = GBufferA.ShaderResourceTexture;
		GBufferResourceStruct.GBufferBTextureNonMS = GBufferB.ShaderResourceTexture;
		GBufferResourceStruct.GBufferCTextureNonMS = GBufferC.ShaderResourceTexture;
		GBufferResourceStruct.GBufferDTextureNonMS = GBufferD.ShaderResourceTexture;
		GBufferResourceStruct.GBufferETextureNonMS = GBufferE.ShaderResourceTexture;

		GBufferResourceStruct.GBufferATextureMS = GBufferA.TargetableTexture;
		GBufferResourceStruct.GBufferBTextureMS = GBufferB.TargetableTexture;
		GBufferResourceStruct.GBufferCTextureMS = GBufferC.TargetableTexture;
		GBufferResourceStruct.GBufferDTextureMS = GBufferD.TargetableTexture;
		GBufferResourceStruct.GBufferETextureMS = GBufferE.TargetableTexture;

		GBufferResourceStruct.GBufferATextureSampler = TStaticSamplerState<>::GetRHI();
		GBufferResourceStruct.GBufferBTextureSampler = TStaticSamplerState<>::GetRHI();
		GBufferResourceStruct.GBufferCTextureSampler = TStaticSamplerState<>::GetRHI();
		GBufferResourceStruct.GBufferDTextureSampler = TStaticSamplerState<>::GetRHI();
		GBufferResourceStruct.GBufferETextureSampler = TStaticSamplerState<>::GetRHI();

		GBufferResourcesUniformBuffer = FGBufferResourceStruct::CreateUniformBuffer(GBufferResourceStruct, UniformBuffer_SingleFrame);
	}

	// so that 
	GBufferRefCount = 1;
}

const TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSceneColor() const
{
	if(!SceneColor)
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// the first called should be AllocSceneColor(), contact MartinM if that happens
			ensure(SceneColor);
		}

		return GSystemTextures.BlackDummy;
	}

	return SceneColor;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetSceneColor()
{
	if(!SceneColor)
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// the first called should be AllocSceneColor(), contact MartinM if that happens
			ensure(SceneColor);
		}

		return GSystemTextures.BlackDummy;
	}

	return SceneColor;
}

void FSceneRenderTargets::SetSceneColor(IPooledRenderTarget* In)
{
	SceneColor = In;
}

void FSceneRenderTargets::SetLightAttenuation(IPooledRenderTarget* In)
{
	LightAttenuation = In;
}

const TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetLightAttenuation() const
{
	if(!LightAttenuation)
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// First we need to call AllocLightAttenuation(), contact MartinM if that happens
			ensure(LightAttenuation);
		}

		return GSystemTextures.WhiteDummy;
	}

	return LightAttenuation;
}

TRefCountPtr<IPooledRenderTarget>& FSceneRenderTargets::GetLightAttenuation()
{
	if(!LightAttenuation)
	{
		// to avoid log/ensure spam
		static bool bFirst = true;
		if(bFirst)
		{
			bFirst = false;

			// the first called should be AllocLightAttenuation(), contact MartinM if that happens
			ensure(LightAttenuation);
		}

		return GSystemTextures.WhiteDummy;
	}

	return LightAttenuation;
}

void FSceneRenderTargets::AdjustGBufferRefCount(int Delta)
{ 
	GBufferRefCount += Delta; 
	
	if(Delta > 0 && GBufferRefCount == 1)
	{
		AllocGBufferTargets();
	}
	else if(GBufferRefCount == 0)
	{
		ReleaseGBufferTargets();
	}
}

void FSceneRenderTargets::FinishRenderingSceneColor(FRHICommandListImmediate& RHICmdList, bool bKeepChanges, const FResolveRect& ResolveRect)
{
	SCOPED_DRAW_EVENT(FinishRenderingSceneColor, DEC_SCENE_ITEMS);

	if(bKeepChanges)
	{
		ResolveSceneColor(RHICmdList);
	}
}

bool FSceneRenderTargets::BeginRenderingCustomDepth(FRHICommandListImmediate& RHICmdList, bool bPrimitives)
{
	IPooledRenderTarget* CustomDepth = RequestCustomDepth(bPrimitives);

	if(CustomDepth)
	{
		SCOPED_DRAW_EVENT(BeginRenderingCustomDepth, DEC_SCENE_ITEMS);
		
		SetRenderTarget(RHICmdList, FTextureRHIRef(), CustomDepth->GetRenderTargetItem().ShaderResourceTexture);

		return true;
	}

	return false;
}

void FSceneRenderTargets::FinishRenderingCustomDepth(FRHICommandListImmediate& RHICmdList, const FResolveRect& ResolveRect)
{
	SCOPED_DRAW_EVENT(FinishRenderingCustomDepth, DEC_SCENE_ITEMS);

	RHICmdList.CopyToResolveTarget(SceneColor->GetRenderTargetItem().TargetableTexture, SceneColor->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams(ResolveRect));

	bCustomDepthIsValid = true;
}

/**
* Saves a previously rendered scene color target
*/
void FSceneRenderTargets::ResolveSceneColor(FRHICommandList& RHICmdList, const FResolveRect& ResolveRect)
{
    SCOPED_DRAW_EVENT(ResolveSceneColor, DEC_SCENE_ITEMS);

	RHICmdList.CopyToResolveTarget(GetSceneColorSurface(), GetSceneColorTexture(), true, FResolveParams(ResolveRect));
}

/** Resolves the GBuffer targets so that their resolved textures can be sampled. */
void FSceneRenderTargets::ResolveGBufferSurfaces(FRHICommandList& RHICmdList, const FResolveRect& ResolveRect)
{
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		SCOPED_DRAW_EVENT(ResolveGBufferSurfaces, DEC_SCENE_ITEMS);

		RHICmdList.CopyToResolveTarget(GSceneRenderTargets.GBufferA->GetRenderTargetItem().TargetableTexture, GSceneRenderTargets.GBufferA->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams(ResolveRect));
		RHICmdList.CopyToResolveTarget(GSceneRenderTargets.GBufferB->GetRenderTargetItem().TargetableTexture, GSceneRenderTargets.GBufferB->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams(ResolveRect));
		RHICmdList.CopyToResolveTarget(GSceneRenderTargets.GBufferC->GetRenderTargetItem().TargetableTexture, GSceneRenderTargets.GBufferC->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams(ResolveRect));
		RHICmdList.CopyToResolveTarget(GSceneRenderTargets.GBufferD->GetRenderTargetItem().TargetableTexture, GSceneRenderTargets.GBufferD->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams(ResolveRect));
		
		if (bAllowStaticLighting)
		{
			RHICmdList.CopyToResolveTarget(GSceneRenderTargets.GBufferE->GetRenderTargetItem().TargetableTexture, GSceneRenderTargets.GBufferE->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams(ResolveRect));
		}
	}
}

void FSceneRenderTargets::BeginRenderingPrePass(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(BeginRenderingPrePass, DEC_SCENE_ITEMS);

	// Set the scene depth surface and a DUMMY buffer as color buffer
	// (as long as it's the same dimension as the depth buffer),
	
	SetRenderTarget(RHICmdList, FTextureRHIRef(), GetSceneDepthSurface());
}

void FSceneRenderTargets::FinishRenderingPrePass(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(FinishRenderingPrePass, DEC_SCENE_ITEMS);
	GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, SceneDepthZ);
}

void FSceneRenderTargets::BeginRenderingShadowDepth(FRHICommandListImmediate& RHICmdList)
{
	GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, ShadowDepthZ);
	SetRenderTarget(RHICmdList, FTextureRHIRef(), GetShadowDepthZSurface());
}

void FSceneRenderTargets::BeginRenderingCubeShadowDepth(FRHICommandListImmediate& RHICmdList, int32 ShadowResolution)
{
	SCOPED_DRAW_EVENT(BeginRenderingCubeShadowDepth, DEC_SCENE_ITEMS);
	SetRenderTarget(RHICmdList, FTextureRHIRef(), GetCubeShadowDepthZSurface(ShadowResolution));
}

void FSceneRenderTargets::FinishRenderingShadowDepth(FRHICommandList& RHICmdList, const FResolveRect& ResolveRect)
{
	// Resolve the shadow depth z surface.
	RHICmdList.CopyToResolveTarget(GetShadowDepthZSurface(), GetShadowDepthZTexture(), false, FResolveParams(ResolveRect));
}

void FSceneRenderTargets::BeginRenderingReflectiveShadowMap(FRHICommandListImmediate& RHICmdList, FLightPropagationVolume* Lpv)
{
	FTextureRHIParamRef RenderTargets[2];
	RenderTargets[0] = GetReflectiveShadowMapNormalSurface();
	RenderTargets[1] = GetReflectiveShadowMapDiffuseSurface();

	// Hook up the geometry volume UAVs
	FUnorderedAccessViewRHIParamRef Uavs[4];
	Uavs[0] = Lpv->GetGvListBufferUav();
	Uavs[1] = Lpv->GetGvListHeadBufferUav();
	Uavs[2] = Lpv->GetVplListBufferUav();
	Uavs[3] = Lpv->GetVplListHeadBufferUav();
	
	SetRenderTargets(RHICmdList, ARRAY_COUNT(RenderTargets), RenderTargets, GetReflectiveShadowMapDepthSurface(), 4, Uavs);
}

void FSceneRenderTargets::FinishRenderingReflectiveShadowMap(FRHICommandList& RHICmdList, const FResolveRect& ResolveRect)
{
	// Resolve the shadow depth z surface.
	RHICmdList.CopyToResolveTarget(GetReflectiveShadowMapDepthSurface(), GetReflectiveShadowMapDepthTexture(), false, FResolveParams(ResolveRect));
	RHICmdList.CopyToResolveTarget(GetReflectiveShadowMapDiffuseSurface(), GetReflectiveShadowMapDiffuseTexture(), false, FResolveParams(ResolveRect));
	RHICmdList.CopyToResolveTarget(GetReflectiveShadowMapNormalSurface(), GetReflectiveShadowMapNormalTexture(), false, FResolveParams(ResolveRect));

	// Unset render targets
	FTextureRHIParamRef RenderTargets[2] = {NULL};
	FUnorderedAccessViewRHIParamRef Uavs[2] = {NULL};
	SetRenderTargets(RHICmdList, ARRAY_COUNT(RenderTargets), RenderTargets, FTextureRHIParamRef(), 2, Uavs);
}

void FSceneRenderTargets::FinishRenderingCubeShadowDepth(FRHICommandList& RHICmdList, int32 ShadowResolution, const FResolveParams& ResolveParams)
{
	SCOPED_DRAW_EVENT(FinishRenderingCubeShadowDepth, DEC_SCENE_ITEMS);
	RHICmdList.CopyToResolveTarget(GetCubeShadowDepthZSurface(ShadowResolution), GetCubeShadowDepthZTexture(ShadowResolution), false, ResolveParams);
}

void FSceneRenderTargets::BeginRenderingSceneAlphaCopy(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(BeginRenderingSceneAlphaCopy, DEC_SCENE_ITEMS);
	GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, GSceneRenderTargets.SceneAlphaCopy);
	SetRenderTarget(RHICmdList, GetSceneAlphaCopySurface(), 0);
}

void FSceneRenderTargets::FinishRenderingSceneAlphaCopy(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_DRAW_EVENT(FinishRenderingSceneAlphaCopy, DEC_SCENE_ITEMS);
	RHICmdList.CopyToResolveTarget(GetSceneAlphaCopySurface(), SceneAlphaCopy->GetRenderTargetItem().ShaderResourceTexture, false, FResolveParams(FResolveRect()));
	GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, GSceneRenderTargets.SceneAlphaCopy);
}


void FSceneRenderTargets::BeginRenderingLightAttenuation(FRHICommandList& RHICmdList)
{
	SCOPED_DRAW_EVENT(BeginRenderingLightAttenuation, DEC_SCENE_ITEMS);

	AllocLightAttenuation();

	GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, GSceneRenderTargets.GetLightAttenuation());

	// Set the light attenuation surface as the render target, and the scene depth buffer as the depth-stencil surface.
	SetRenderTarget(RHICmdList, GetLightAttenuationSurface(),GetSceneDepthSurface());
}

void FSceneRenderTargets::FinishRenderingLightAttenuation(FRHICommandList& RHICmdList)
{
	SCOPED_DRAW_EVENT(FinishRenderingLightAttenuation, DEC_SCENE_ITEMS);

	// Resolve the light attenuation surface.
	RHICmdList.CopyToResolveTarget(GetLightAttenuationSurface(), LightAttenuation->GetRenderTargetItem().ShaderResourceTexture, false, FResolveParams(FResolveRect()));
	
	GRenderTargetPool.VisualizeTexture.SetCheckPoint(RHICmdList, GSceneRenderTargets.GetLightAttenuation());
}

void FSceneRenderTargets::BeginRenderingTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	// Use the scene color buffer.
	GSceneRenderTargets.BeginRenderingSceneColor(RHICmdList);
	
	// viewport to match view size
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
}


bool FSceneRenderTargets::BeginRenderingSeparateTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View, bool bFirstTimeThisFrame)
{
	if(IsSeparateTranslucencyActive(View))
	{
		SCOPED_DRAW_EVENT(BeginSeparateTranslucency, DEC_SCENE_ITEMS);

		FSceneViewState* ViewState = (FSceneViewState*)View.State;

		// the RT should only be available for a short period during rendering
		if(bFirstTimeThisFrame)
		{
			check(!ViewState->SeparateTranslucencyRT);
		}

		TRefCountPtr<IPooledRenderTarget>& SeparateTranslucency = ViewState->GetSeparateTranslucency(View);

		// Use a separate render target for translucency
		SetRenderTarget(RHICmdList, SeparateTranslucency->GetRenderTargetItem().TargetableTexture, GetSceneDepthSurface());
		
		if(bFirstTimeThisFrame)
		{
			RHICmdList.Clear(true, FLinearColor(0, 0, 0, 1), false, 0, false, 0, FIntRect());
		}

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		return true;
	}

	return false;
}

void FSceneRenderTargets::FinishRenderingSeparateTranslucency(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	if(IsSeparateTranslucencyActive(View))
	{
		FSceneViewState* ViewState = (FSceneViewState*)View.State;
		TRefCountPtr<IPooledRenderTarget>& SeparateTranslucency = ViewState->GetSeparateTranslucency(View);

		RHICmdList.CopyToResolveTarget(SeparateTranslucency->GetRenderTargetItem().TargetableTexture, SeparateTranslucency->GetRenderTargetItem().ShaderResourceTexture, true, FResolveParams());
	}
}

void FSceneRenderTargets::ResolveSceneDepthTexture(FRHICommandList& RHICmdList)
{
	SCOPED_DRAW_EVENT(ResolveSceneDepthTexture, DEC_SCENE_ITEMS);

	RHICmdList.CopyToResolveTarget(GetSceneDepthSurface(), GetSceneDepthTexture(), true, FResolveParams());
}

void FSceneRenderTargets::ResolveSceneDepthToAuxiliaryTexture(FRHICommandList& RHICmdList)
{
	// Resolve the scene depth to an auxiliary texture when SM3/SM4 is in use. This needs to happen so the auxiliary texture can be bound as a shader parameter
	// while the primary scene depth texture can be bound as the target. Simultaneously binding a single DepthStencil resource as a parameter and target
	// is unsupported in d3d feature level 10.
	if(!GSupportsDepthFetchDuringDepthTest)
	{
		SCOPED_DRAW_EVENT(ResolveSceneDepthToAuxiliaryTexture, DEC_SCENE_ITEMS);

		RHICmdList.CopyToResolveTarget(GetSceneDepthSurface(), GetAuxiliarySceneDepthTexture(), true, FResolveParams());
	}
}

void FSceneRenderTargets::CleanUpEditorPrimitiveTargets()
{
	EditorPrimitivesDepth.SafeRelease();
	EditorPrimitivesColor.SafeRelease();
}

int32 FSceneRenderTargets::GetEditorMSAACompositingSampleCount() const
{
	int32 Value = 1;

	// only supported on SM5 yet (SM4 doesn't have MSAA sample load functionality which makes it harder to implement)
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MSAA.CompositingSampleCount"));

		Value = CVar->GetValueOnRenderThread();

		if(Value <= 1)
		{
			Value = 1;
		}
		else if(Value <= 2)
		{
			Value = 2;
		}
		else if(Value <= 4)
		{
			Value = 4;
		}
		else
		{
			Value = 8;
		}
	}

	return Value;
}

const FTexture2DRHIRef& FSceneRenderTargets::GetEditorPrimitivesColor()
{
	const bool bIsValid = IsValidRef(EditorPrimitivesColor);

	if( !bIsValid || EditorPrimitivesColor->GetDesc().NumSamples != GetEditorMSAACompositingSampleCount() )
	{
		// If the target is does not match the MSAA settings it needs to be recreated
		InitEditorPrimitivesColor();
	}

	return (const FTexture2DRHIRef&)EditorPrimitivesColor->GetRenderTargetItem().TargetableTexture;
}


const FTexture2DRHIRef& FSceneRenderTargets::GetEditorPrimitivesDepth()
{
	const bool bIsValid = IsValidRef(EditorPrimitivesDepth);

	if( !bIsValid || EditorPrimitivesDepth->GetDesc().NumSamples != GetEditorMSAACompositingSampleCount() )
	{
		// If the target is does not match the MSAA settings it needs to be recreated
		InitEditorPrimitivesDepth();
	}

	return (const FTexture2DRHIRef&)EditorPrimitivesDepth->GetRenderTargetItem().TargetableTexture;
}

static TAutoConsoleVariable<int32> CVarSetSeperateTranslucencyEnabled(
	TEXT("r.SeparateTranslucency"),
	1,
	TEXT("Allows to disable the separate translucency feature (all translucency is rendered in separate RT and composited\n")
	TEXT("after DOF, if not specified otherwise in the material).\n")
	TEXT(" 0: off (translucency is affected by depth of field)\n")
	TEXT(" 1: on costs GPU performance and memory but keeps translucency unaffected by Depth of Fieled. (default)"),
	ECVF_RenderThreadSafe);

bool FSceneRenderTargets::IsSeparateTranslucencyActive(const FViewInfo& View) const
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SeparateTranslucency"));
	int32 Value = CVar->GetValueOnRenderThread();

	return (Value != 0) && CurrentFeatureLevel >= ERHIFeatureLevel::SM4
		&& View.Family->EngineShowFlags.PostProcessing
		&& View.Family->EngineShowFlags.SeparateTranslucency
		&& View.State != NULL;	// We require a ViewState in order for separate translucency to work (it keeps track of our SeparateTranslucencyRT)
}

void FSceneRenderTargets::InitEditorPrimitivesColor()
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, 
		PF_B8G8R8A8, 
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_RenderTargetable,
		false));

	Desc.NumSamples = GetEditorMSAACompositingSampleCount();

	GRenderTargetPool.FindFreeElement(Desc, EditorPrimitivesColor, TEXT("EditorPrimitivesColor"));
}

void FSceneRenderTargets::InitEditorPrimitivesDepth()
{
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, 
		PF_DepthStencil, 
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_DepthStencilTargetable,
		false));

	Desc.NumSamples = GetEditorMSAACompositingSampleCount();

	GRenderTargetPool.FindFreeElement(Desc, EditorPrimitivesDepth, TEXT("EditorPrimitivesDepth"));
}

void FSceneRenderTargets::QuantizeBufferSize(int32& InOutBufferSizeX, int32& InOutBufferSizeY) const
{
	// ensure sizes are dividable by DividableBy to get post processing effects with lower resolution working well
	const uint32 DividableBy = 8;

	const uint32 Mask = ~(DividableBy - 1);
	InOutBufferSizeX = (InOutBufferSizeX + DividableBy - 1) & Mask;
	InOutBufferSizeY = (InOutBufferSizeY + DividableBy - 1) & Mask;
}

void FSceneRenderTargets::SetBufferSize(int32 InBufferSizeX, int32 InBufferSizeY)
{
	QuantizeBufferSize(InBufferSizeX, InBufferSizeY);
	BufferSize.X = InBufferSizeX;
	BufferSize.Y = InBufferSizeY;
}

void FSceneRenderTargets::AllocateForwardShadingPathRenderTargets()
{
	// on ES2 we don't do on demand allocation of SceneColor yet (in non ES2 it's released in the Tonemapper Process())
	AllocSceneColor();

	EPixelFormat Format = SceneColor->GetDesc().Format;

	{
		// Create a texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		Desc.NumSamples = SceneColor->GetDesc().NumSamples;
		GRenderTargetPool.FindFreeElement(Desc, SceneDepthZ, TEXT("SceneDepthZ"));
	}

	// For 64-bit ES2 without framebuffer fetch, create extra render target for copy of alpha channel.
	if((Format == PF_FloatRGBA) && (GSupportsShaderFramebufferFetch == false)) 
	{
#if PLATFORM_HTML5 || PLATFORM_ANDROID
		// creating a PF_R16F (a true one-channel renderable fp texture) is only supported on GL if EXT_texture_rg is available.  It's present
		// on iOS, but not in WebGL or Android.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_FloatRGBA, TexCreate_None, TexCreate_RenderTargetable, false));
#else
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_R16F, TexCreate_None, TexCreate_RenderTargetable, false));
#endif
		GRenderTargetPool.FindFreeElement(Desc, SceneAlphaCopy, TEXT("SceneAlphaCopy"));
	}
	else
	{
		SceneAlphaCopy = GSystemTextures.MaxFP16Depth;
	}

	// When targeting DX Feature Level 10, create an auxiliary texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
	if (!GSupportsDepthFetchDuringDepthTest)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, AuxiliarySceneDepthZ, TEXT("AuxiliarySceneDepthZ"));
	}
}

// for easier use of "VisualizeTexture"
static TCHAR* const GetVolumeName(uint32 Id, bool bDirectional)
{
	// (TCHAR*) for non VisualStudio
	switch(Id)
	{
		case 0: return bDirectional ? (TCHAR*)TEXT("TranslucentVolumeDir0") : (TCHAR*)TEXT("TranslucentVolume0");
		case 1: return bDirectional ? (TCHAR*)TEXT("TranslucentVolumeDir1") : (TCHAR*)TEXT("TranslucentVolume1");
		case 2: return bDirectional ? (TCHAR*)TEXT("TranslucentVolumeDir2") : (TCHAR*)TEXT("TranslucentVolume2");

		default:
			check(0);
	}
	return (TCHAR*)TEXT("InvalidName");
}


// for easier use of "VisualizeTexture"
static TCHAR* const GetTranslucencyShadowTransmissionName(uint32 Id)
{
	// (TCHAR*) for non VisualStudio
	switch(Id)
	{
		case 0: return (TCHAR*)TEXT("TranslucencyShadowTransmission0");
		case 1: return (TCHAR*)TEXT("TranslucencyShadowTransmission1");

		default:
			check(0);
	}
	return (TCHAR*)TEXT("InvalidName");
}


void FSceneRenderTargets::AllocateDeferredShadingPathRenderTargets()
{
	{
		// Create a texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		Desc.Flags |= TexCreate_FastVRAM;
		GRenderTargetPool.FindFreeElement(Desc, SceneDepthZ, TEXT("SceneDepthZ"));
	}

	// When targeting DX Feature Level 10, create an auxiliary texture to store the resolved scene depth, and a render-targetable surface to hold the unresolved scene depth.
	if(!GSupportsDepthFetchDuringDepthTest)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, AuxiliarySceneDepthZ, TEXT("AuxiliarySceneDepthZ"));
	}

	// Create a quarter-sized version of the scene depth.
	{
		FIntPoint SmallDepthZSize(FMath::Max<uint32>(BufferSize.X / SmallColorDepthDownsampleFactor, 1), FMath::Max<uint32>(BufferSize.Y / SmallColorDepthDownsampleFactor, 1));

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(SmallDepthZSize, PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable, true));
		GRenderTargetPool.FindFreeElement(Desc, SmallDepthZ, TEXT("SmallDepthZ"));
	}

	// Set up quarter size scene color shared texture
	const FIntPoint ShadowBufferResolution = GetShadowDepthTextureResolution();

	const FIntPoint TranslucentShadowBufferResolution = GetTranslucentShadowDepthTextureResolution();

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		for (int32 SurfaceIndex = 0; SurfaceIndex < NumTranslucencyShadowSurfaces; SurfaceIndex++)
		{
			if (!TranslucencyShadowTransmission[SurfaceIndex])
			{
				// Using PF_FloatRGBA because Fourier coefficients used by Fourier opacity maps have a large range and can be negative
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(TranslucentShadowBufferResolution, PF_FloatRGBA, TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(Desc, TranslucencyShadowTransmission[SurfaceIndex], GetTranslucencyShadowTransmissionName(SurfaceIndex));
			}
		}
	}

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		// Create several shadow depth cube maps with different resolutions, to handle different sized shadows on the screen
		for (int32 SurfaceIndex = 0; SurfaceIndex < NumCubeShadowDepthSurfaces; SurfaceIndex++)
		{
			const int32 SurfaceResolution = GetCubeShadowDepthZResolution(SurfaceIndex);

			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateCubemapDesc(SurfaceResolution, PF_ShadowDepth, TexCreate_None, TexCreate_DepthStencilTargetable, false));
			GRenderTargetPool.FindFreeElement(Desc, CubeShadowDepthZ[SurfaceIndex], TEXT("CubeShadowDepthZ[]"));
		}
	}

	//create the shadow depth texture and/or surface
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(ShadowBufferResolution, PF_ShadowDepth, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, ShadowDepthZ, TEXT("ShadowDepthZ"));
	}
		
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetPreShadowCacheTextureResolution(), PF_ShadowDepth, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, PreShadowCacheDepthZ, TEXT("PreShadowCacheDepthZ"));
		// Mark the preshadow cache as newly allocated, so the cache will know to update
		bPreshadowCacheNewlyAllocated = true;
	}

	// Create the required render targets if running Highend.
	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		// Create the screen space ambient occlusion buffer
		{
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_G8, TexCreate_None, TexCreate_RenderTargetable, false));
			GRenderTargetPool.FindFreeElement(Desc, ScreenSpaceAO, TEXT("ScreenSpaceAO"));
		}

		{
			for (int32 RTSetIndex = 0; RTSetIndex < NumTranslucentVolumeRenderTargetSets; RTSetIndex++)
			{
				GRenderTargetPool.FindFreeElement(
					FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						GTranslucencyLightingVolumeDim,
						GTranslucencyLightingVolumeDim,
						GTranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						0,
						TexCreate_ShaderResource | TexCreate_RenderTargetable,
						false)),
					TranslucencyLightingVolumeAmbient[RTSetIndex],
					GetVolumeName(RTSetIndex, false)
					);

				GRenderTargetPool.FindFreeElement(
					FPooledRenderTargetDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
						GTranslucencyLightingVolumeDim,
						GTranslucencyLightingVolumeDim,
						GTranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						0,
						TexCreate_ShaderResource | TexCreate_RenderTargetable,
						false)),
					TranslucencyLightingVolumeDirectional[RTSetIndex],
					GetVolumeName(RTSetIndex, true)
					);
			}
		}
	}

	extern ENGINE_API int32 GReflectionCaptureSize;
	const int32 NumReflectionCaptureMips = FMath::CeilLogTwo(GReflectionCaptureSize) + 1;

	{
		uint32 TexFlags = TexCreate_None;

		if (!GSupportsGSRenderTargetLayerSwitchingToMips)
		{
			TexFlags = TexCreate_TargetArraySlicesIndependently;
		}

		// Create scratch cubemaps for filtering passes
		FPooledRenderTargetDesc Desc2(FPooledRenderTargetDesc::CreateCubemapDesc(GReflectionCaptureSize, PF_FloatRGBA, TexFlags, TexCreate_RenderTargetable, false, 1, NumReflectionCaptureMips));
		GRenderTargetPool.FindFreeElement(Desc2, ReflectionColorScratchCubemap[0], TEXT("ReflectionColorScratchCubemap0"));
		GRenderTargetPool.FindFreeElement(Desc2, ReflectionColorScratchCubemap[1], TEXT("ReflectionColorScratchCubemap1"));

		FPooledRenderTargetDesc Desc3(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(1, 1), PF_R32_FLOAT, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc3, ReflectionBrightness, TEXT("ReflectionBrightness"));
	}

	extern int32 GDiffuseIrradianceCubemapSize;
	const int32 NumDiffuseIrradianceMips = FMath::CeilLogTwo(GDiffuseIrradianceCubemapSize) + 1;

	{
		FPooledRenderTargetDesc Desc2(FPooledRenderTargetDesc::CreateCubemapDesc(GDiffuseIrradianceCubemapSize, PF_FloatRGBA, TexCreate_None, TexCreate_RenderTargetable, false, 1, NumDiffuseIrradianceMips));
		GRenderTargetPool.FindFreeElement(Desc2, DiffuseIrradianceScratchCubemap[0], TEXT("DiffuseIrradianceScratchCubemap0"));
		GRenderTargetPool.FindFreeElement(Desc2, DiffuseIrradianceScratchCubemap[1], TEXT("DiffuseIrradianceScratchCubemap1"));
	}

	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(FIntPoint(FSHVector3::MaxSHBasis, 1), PF_FloatRGBA, TexCreate_None, TexCreate_RenderTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, SkySHIrradianceMap, TEXT("SkySHIrradianceMap"));
	}

	if (CurrentFeatureLevel >= ERHIFeatureLevel::SM5)
	{
		// Create the reflective shadow map textures for LightPropagationVolume feature
		if(bCurrentLightPropagationVolume)
		{
			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetReflectiveShadowMapTextureResolution(), PF_R8G8B8A8, TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(Desc, ReflectiveShadowMapNormal, TEXT("RSMNormal"));
			}

			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetReflectiveShadowMapTextureResolution(), PF_FloatR11G11B10, TexCreate_None, TexCreate_RenderTargetable, false));
				GRenderTargetPool.FindFreeElement(Desc, ReflectiveShadowMapDiffuse, TEXT("RSMDiffuse"));
			}

			{
				FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetReflectiveShadowMapTextureResolution(), PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable , false));
				GRenderTargetPool.FindFreeElement(Desc, ReflectiveShadowMapDepth, TEXT("RSMDepth"));
			}
		}
	}
}

EPixelFormat FSceneRenderTargets::GetSceneColorFormat() const
{
	EPixelFormat SceneColorBufferFormat = PF_FloatRGBA;

	if (CurrentFeatureLevel <= ERHIFeatureLevel::ES2)
	{
		// Potentially allocate an alpha channel in th -fe scene color texture to store the resolved scene depth.
		SceneColorBufferFormat = GSupportsRenderTargetFormat_PF_FloatRGBA ? PF_FloatRGBA : PF_B8G8R8A8;
		if (!IsMobileHDR() || IsMobileHDR32bpp()) 
		{
			SceneColorBufferFormat = PF_B8G8R8A8;
		}
	}
	else
	{
	switch(CurrentSceneColorFormat)
	{
		case 0:
			SceneColorBufferFormat = PF_R8G8B8A8; break;
		case 1:
			SceneColorBufferFormat = PF_A2B10G10R10; break;
		case 2:	
			SceneColorBufferFormat = PF_FloatR11G11B10; break;
		case 3:	
			SceneColorBufferFormat = PF_FloatRGB; break;
		case 4:
			// default
			break;
		case 5:
			SceneColorBufferFormat = PF_A32B32G32R32F; break;
	}
	}

	return SceneColorBufferFormat;
}

void FSceneRenderTargets::InitDynamicRHI()
{
	if(BufferSize.X > 0 && BufferSize.Y > 0)
	{
		// start with a defined state for the scissor rect (D3D11 was returning (0,0,0,0) which caused a clear to not execute correctly)
		// todo: move this to an earlier place (for dx9 is has to be after device creation which is after window creation)
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

		if (CurrentFeatureLevel == ERHIFeatureLevel::ES2)
		{
			AllocateForwardShadingPathRenderTargets();
		}
		else
		{
			AllocateDeferredShadingPathRenderTargets();
		}
	}
}

void FSceneRenderTargets::ReleaseAllTargets()
{
	ReleaseGBufferTargets();

	SceneColor.SafeRelease();
	SceneAlphaCopy.SafeRelease();
	SceneDepthZ.SafeRelease();
	AuxiliarySceneDepthZ.SafeRelease();
	SmallDepthZ.SafeRelease();
	DBufferA.SafeRelease();
	DBufferB.SafeRelease();
	DBufferC.SafeRelease();
	ScreenSpaceAO.SafeRelease();
	LightAttenuation.SafeRelease();
	CustomDepth.SafeRelease();
	ReflectiveShadowMapNormal.SafeRelease();
	ReflectiveShadowMapDiffuse.SafeRelease();
	ReflectiveShadowMapDepth.SafeRelease();

	for (int32 SurfaceIndex = 0; SurfaceIndex < NumTranslucencyShadowSurfaces; SurfaceIndex++)
	{
		TranslucencyShadowTransmission[SurfaceIndex].SafeRelease();
	}

	ShadowDepthZ.SafeRelease();
	PreShadowCacheDepthZ.SafeRelease();
	
	for(int32 Index = 0; Index < NumCubeShadowDepthSurfaces; ++Index)
	{
		CubeShadowDepthZ[Index].SafeRelease();
	}

	for (int32 i = 0; i < ARRAY_COUNT(ReflectionColorScratchCubemap); i++)
	{
		ReflectionColorScratchCubemap[i].SafeRelease();
	}

	ReflectionBrightness.SafeRelease();

	for (int32 i = 0; i < ARRAY_COUNT(DiffuseIrradianceScratchCubemap); i++)
	{
		DiffuseIrradianceScratchCubemap[i].SafeRelease();
	}

	SkySHIrradianceMap.SafeRelease();

	for (int32 RTSetIndex = 0; RTSetIndex < NumTranslucentVolumeRenderTargetSets; RTSetIndex++)
	{
		TranslucencyLightingVolumeAmbient[RTSetIndex].SafeRelease();
		TranslucencyLightingVolumeDirectional[RTSetIndex].SafeRelease();
	}

	EditorPrimitivesColor.SafeRelease();
	EditorPrimitivesDepth.SafeRelease();
}

void FSceneRenderTargets::ReleaseDynamicRHI()
{
	ReleaseAllTargets();
	GRenderTargetPool.FreeUnusedResources();
}

/** Returns the size of the shadow depth buffer, taking into account platform limitations and game specific resolution limits. */
FIntPoint FSceneRenderTargets::GetShadowDepthTextureResolution() const
{
	int32 MaxShadowRes = GetCachedScalabilityCVars().MaxShadowResolution;
	const FIntPoint ShadowBufferResolution(
			FMath::Clamp(MaxShadowRes,1,GMaxShadowDepthBufferSizeX),
			FMath::Clamp(MaxShadowRes,1,GMaxShadowDepthBufferSizeY));
	
	return ShadowBufferResolution;
}

FIntPoint FSceneRenderTargets::GetReflectiveShadowMapTextureResolution() const
{
	return FIntPoint( ReflectiveShadowMapResolution, ReflectiveShadowMapResolution );
}

FIntPoint FSceneRenderTargets::GetPreShadowCacheTextureResolution() const
{
	const FIntPoint ShadowDepthResolution = GetShadowDepthTextureResolution();
	// Higher numbers increase cache hit rate but also memory usage
	const int32 ExpandFactor = 2;

	static auto CVarPreShadowResolutionFactor = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.Shadow.PreShadowResolutionFactor"));
	return FIntPoint(FMath::TruncToInt(ShadowDepthResolution.X * CVarPreShadowResolutionFactor->GetValueOnRenderThread()), FMath::TruncToInt(ShadowDepthResolution.Y * CVarPreShadowResolutionFactor->GetValueOnRenderThread())) * ExpandFactor;
}

FIntPoint FSceneRenderTargets::GetTranslucentShadowDepthTextureResolution() const
{
	const FIntPoint ShadowDepthResolution = GetShadowDepthTextureResolution();
	return ShadowDepthResolution;
}

const FTextureRHIRef& FSceneRenderTargets::GetSceneColorSurface() const							
{
	if(!SceneColor)
	{
		return GBlackTexture->TextureRHI;
	}

	return (const FTextureRHIRef&)SceneColor->GetRenderTargetItem().TargetableTexture; 
}

const FTextureRHIRef& FSceneRenderTargets::GetSceneColorTexture() const
{
	if(!SceneColor)
	{
		return GBlackTexture->TextureRHI;
	}

	return (const FTextureRHIRef&)GetSceneColor()->GetRenderTargetItem().ShaderResourceTexture; 
}

IPooledRenderTarget* FSceneRenderTargets::RequestCustomDepth(bool bPrimitives)
{
	int Value = CVarCustomDepth.GetValueOnRenderThread();

	if((Value == 1  && bPrimitives) || Value == 2)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(BufferSize, PF_DepthStencil, TexCreate_None, TexCreate_DepthStencilTargetable, false));
		GRenderTargetPool.FindFreeElement(Desc, CustomDepth, TEXT("CustomDepth"));
		return CustomDepth;
	}

	return 0;
}

/** Returns an index in the range [0, NumCubeShadowDepthSurfaces) given an input resolution. */
int32 FSceneRenderTargets::GetCubeShadowDepthZIndex(int32 ShadowResolution) const
{
	static auto CVarMinShadowResolution = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));
	FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution();

	// Use a lower resolution because cubemaps use a lot of memory
	ObjectShadowBufferResolution.X /= 2;
	ObjectShadowBufferResolution.Y /= 2;
	const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =
	{
		ObjectShadowBufferResolution.X,
		ObjectShadowBufferResolution.X / 2,
		ObjectShadowBufferResolution.X / 4,
		ObjectShadowBufferResolution.X / 8,
		CVarMinShadowResolution->GetValueOnRenderThread()
	};

	for (int32 SearchIndex = 0; SearchIndex < NumCubeShadowDepthSurfaces; SearchIndex++)
	{
		if (ShadowResolution >= SurfaceSizes[SearchIndex])
		{
			return SearchIndex;
		}
	}

	check(0);
	return 0;
}

/** Returns the appropriate resolution for a given cube shadow index. */
int32 FSceneRenderTargets::GetCubeShadowDepthZResolution(int32 ShadowIndex) const
{
	checkSlow(ShadowIndex >= 0 && ShadowIndex < NumCubeShadowDepthSurfaces);

	static auto CVarMinShadowResolution = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Shadow.MinResolution"));
	FIntPoint ObjectShadowBufferResolution = GetShadowDepthTextureResolution();

	// Use a lower resolution because cubemaps use a lot of memory
	ObjectShadowBufferResolution.X = FMath::Max(ObjectShadowBufferResolution.X / 2, 1);
	ObjectShadowBufferResolution.Y = FMath::Max(ObjectShadowBufferResolution.Y / 2, 1);
	const int32 SurfaceSizes[NumCubeShadowDepthSurfaces] =
	{
		ObjectShadowBufferResolution.X,
		FMath::Max(ObjectShadowBufferResolution.X / 2, 1),
		FMath::Max(ObjectShadowBufferResolution.X / 4, 1),
		FMath::Max(ObjectShadowBufferResolution.X / 8, 1),
		CVarMinShadowResolution->GetValueOnRenderThread()
	};
	return SurfaceSizes[ShadowIndex];
}

/*-----------------------------------------------------------------------------
FSceneTextureShaderParameters
-----------------------------------------------------------------------------*/

//
void FSceneTextureShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	// only used if Material has an expression that requires SceneColorTexture
	SceneColorTextureParameter.Bind(ParameterMap,TEXT("SceneColorTexture"));
	SceneColorTextureParameterSampler.Bind(ParameterMap,TEXT("SceneColorTextureSampler"));
	// only used if Material has an expression that requires SceneDepthTexture
	SceneDepthTextureParameter.Bind(ParameterMap,TEXT("SceneDepthTexture"));
	SceneDepthTextureParameterSampler.Bind(ParameterMap,TEXT("SceneDepthTextureSampler"));
	// Only used if Material has an expression that requires SceneAlphaCopyTexture
	SceneAlphaCopyTextureParameter.Bind(ParameterMap,TEXT("SceneAlphaCopyTexture"));
	SceneAlphaCopyTextureParameterSampler.Bind(ParameterMap,TEXT("SceneAlphaCopyTextureSampler"));
	//
	SceneDepthTextureNonMS.Bind(ParameterMap,TEXT("SceneDepthTextureNonMS"));
	SceneColorSurfaceParameter.Bind(ParameterMap,TEXT("SceneColorSurface"));
	// only used if Material has an expression that requires SceneColorTextureMSAA
	SceneDepthSurfaceParameter.Bind(ParameterMap,TEXT("SceneDepthSurface"));
}

template< typename ShaderRHIParamRef >
void FSceneTextureShaderParameters::Set(
	FRHICommandList& RHICmdList,
	const ShaderRHIParamRef& ShaderRHI,
	const FSceneView& View,
	ESceneRenderTargetsMode::Type TextureMode,
	ESamplerFilter ColorFilter ) const
{
	if (TextureMode == ESceneRenderTargetsMode::SetTextures)
	{
		// optimization possible: TShaderRHIParamRef is no param Ref
		if (SceneColorTextureParameter.IsBound())
		{
			FSamplerStateRHIRef Filter;
			switch ( ColorFilter )
			{
			case SF_Bilinear:
				Filter = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
				break;
			case SF_Trilinear:
				Filter = TStaticSamplerState<SF_Trilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
				break;
			case SF_AnisotropicPoint:
				Filter = TStaticSamplerState<SF_AnisotropicPoint,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
				break;
			case SF_AnisotropicLinear:
				Filter = TStaticSamplerState<SF_AnisotropicLinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
				break;
			case SF_Point:
			default:
				Filter = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
				break;
			}

			SetTextureParameter(
				RHICmdList, 
				ShaderRHI,
				SceneColorTextureParameter,
				SceneColorTextureParameterSampler,
				Filter,
				GSceneRenderTargets.GetSceneColorTexture()
				);
		}

		if (SceneAlphaCopyTextureParameter.IsBound() && GSceneRenderTargets.HasSceneAlphaCopyTexture())
		{
			FSamplerStateRHIRef Filter;
			Filter = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
			SetTextureParameter(
				RHICmdList, 
				ShaderRHI,
				SceneAlphaCopyTextureParameter,
				SceneAlphaCopyTextureParameterSampler,
				Filter,
				GSceneRenderTargets.GetSceneAlphaCopyTexture()
				);
		}

		if(SceneDepthTextureParameter.IsBound() || SceneDepthTextureParameterSampler.IsBound())
		{
			const FTexture2DRHIRef* DepthTexture = GSceneRenderTargets.GetActualDepthTexture();
			SetTextureParameter(
				RHICmdList, 
				ShaderRHI,
				SceneDepthTextureParameter,
				SceneDepthTextureParameterSampler,
				TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				*DepthTexture
				);
		}

		const auto FeatureLevel = View.GetFeatureLevel();

		if (FeatureLevel >= ERHIFeatureLevel::SM5)
		{
			SetTextureParameter(RHICmdList, ShaderRHI, SceneColorSurfaceParameter, GSceneRenderTargets.GetSceneColorSurface());
		}
		if (FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			if(GSupportsDepthFetchDuringDepthTest)
			{
				if(SceneDepthSurfaceParameter.IsBound())
				{
					SetTextureParameter(RHICmdList, ShaderRHI, SceneDepthSurfaceParameter, GSceneRenderTargets.GetSceneDepthSurface());
				}
				if(SceneDepthTextureNonMS.IsBound())
				{
					SetTextureParameter(RHICmdList, ShaderRHI, SceneDepthTextureNonMS, GSceneRenderTargets.GetSceneDepthTexture());
				}
			}
			else
			{
				if(SceneDepthSurfaceParameter.IsBound())
				{
					SetTextureParameter(RHICmdList, ShaderRHI, SceneDepthSurfaceParameter, GSceneRenderTargets.GetAuxiliarySceneDepthSurface());
				}
				if(SceneDepthTextureNonMS.IsBound())
				{
					SetTextureParameter(RHICmdList, ShaderRHI, SceneDepthTextureNonMS, GSceneRenderTargets.GetAuxiliarySceneDepthSurface());
				}
			}
		}
	}
	else if (TextureMode == ESceneRenderTargetsMode::DontSet)
	{
		// Verify that none of these were bound if we were told not to set them
		checkSlow(!SceneColorTextureParameter.IsBound()
			&& !SceneDepthTextureParameter.IsBound()
			&& !SceneColorSurfaceParameter.IsBound()
			&& !SceneDepthSurfaceParameter.IsBound()
			&& !SceneDepthTextureNonMS.IsBound());
	}
	else if (TextureMode == ESceneRenderTargetsMode::DontSetIgnoreBoundByEditorCompositing)
	{
		// Verify that none of these were bound if we were told not to set them
		// ignore SceneDepthTextureNonMS
		checkSlow(!SceneColorTextureParameter.IsBound()
			&& !SceneDepthTextureParameter.IsBound()
			&& !SceneColorSurfaceParameter.IsBound()
			&& !SceneDepthSurfaceParameter.IsBound());
	}
	else if( TextureMode == ESceneRenderTargetsMode::NonSceneAlignedPass )
	{
		FSamplerStateRHIParamRef DefaultSampler = TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
		SetTextureParameter(RHICmdList, ShaderRHI, SceneColorTextureParameter, SceneColorTextureParameterSampler, DefaultSampler, GBlackTexture->TextureRHI);
		SetTextureParameter(RHICmdList, ShaderRHI, SceneDepthTextureParameter, SceneDepthTextureParameterSampler, DefaultSampler, GBlackTexture->TextureRHI);
		SetTextureParameter(RHICmdList, ShaderRHI, SceneColorSurfaceParameter, GBlackTexture->TextureRHI);
		SetTextureParameter(RHICmdList, ShaderRHI, SceneDepthSurfaceParameter, GBlackTexture->TextureRHI);
	}
}

#define IMPLEMENT_SCENE_TEXTURE_PARAM_SET( ShaderRHIParamRef ) \
	template void FSceneTextureShaderParameters::Set< ShaderRHIParamRef >( \
		FRHICommandList& RHICmdList,				\
		const ShaderRHIParamRef& ShaderRHI,			\
		const FSceneView& View,						\
		ESceneRenderTargetsMode::Type TextureMode,	\
		ESamplerFilter ColorFilter					\
	) const;

IMPLEMENT_SCENE_TEXTURE_PARAM_SET( FVertexShaderRHIParamRef );
IMPLEMENT_SCENE_TEXTURE_PARAM_SET( FHullShaderRHIParamRef );
IMPLEMENT_SCENE_TEXTURE_PARAM_SET( FDomainShaderRHIParamRef );
IMPLEMENT_SCENE_TEXTURE_PARAM_SET( FGeometryShaderRHIParamRef );
IMPLEMENT_SCENE_TEXTURE_PARAM_SET( FPixelShaderRHIParamRef );
IMPLEMENT_SCENE_TEXTURE_PARAM_SET( FComputeShaderRHIParamRef );

FArchive& operator<<(FArchive& Ar,FSceneTextureShaderParameters& Parameters)
{
	Ar << Parameters.SceneColorTextureParameter;
	Ar << Parameters.SceneColorTextureParameterSampler;
	Ar << Parameters.SceneAlphaCopyTextureParameter;
	Ar << Parameters.SceneAlphaCopyTextureParameterSampler;
	Ar << Parameters.SceneColorSurfaceParameter;
	Ar << Parameters.SceneDepthTextureParameter;
	Ar << Parameters.SceneDepthTextureParameterSampler;
	Ar << Parameters.SceneDepthSurfaceParameter;
	Ar << Parameters.SceneDepthTextureNonMS;
	return Ar;
}

// Note this is not just for Deferred rendering, it also applies to mobile forward rendering.
void FDeferredPixelShaderParameters::Bind(const FShaderParameterMap& ParameterMap)
{
	SceneTextureParameters.Bind(ParameterMap);
	
	GBufferResources.Bind(ParameterMap,TEXT("GBuffers"));
	DBufferATextureMS.Bind(ParameterMap,TEXT("DBufferATextureMS"));
	DBufferBTextureMS.Bind(ParameterMap,TEXT("DBufferBTextureMS"));
	DBufferCTextureMS.Bind(ParameterMap,TEXT("DBufferCTextureMS"));
	ScreenSpaceAOTextureMS.Bind(ParameterMap,TEXT("ScreenSpaceAOTextureMS"));
	DBufferATextureNonMS.Bind(ParameterMap,TEXT("DBufferATextureNonMS"));
	DBufferBTextureNonMS.Bind(ParameterMap,TEXT("DBufferBTextureNonMS"));
	DBufferCTextureNonMS.Bind(ParameterMap,TEXT("DBufferCTextureNonMS"));
	ScreenSpaceAOTextureNonMS.Bind(ParameterMap,TEXT("ScreenSpaceAOTextureNonMS"));
	CustomDepthTextureNonMS.Bind(ParameterMap,TEXT("CustomDepthTextureNonMS"));
	DBufferATexture.Bind(ParameterMap,TEXT("DBufferATexture"));
	DBufferATextureSampler.Bind(ParameterMap,TEXT("DBufferATextureSampler"));
	DBufferBTexture.Bind(ParameterMap,TEXT("DBufferBTexture"));
	DBufferBTextureSampler.Bind(ParameterMap,TEXT("DBufferBTextureSampler"));
	DBufferCTexture.Bind(ParameterMap,TEXT("DBufferCTexture"));
	DBufferCTextureSampler.Bind(ParameterMap,TEXT("DBufferCTextureSampler"));
	ScreenSpaceAOTexture.Bind(ParameterMap,TEXT("ScreenSpaceAOTexture"));
	ScreenSpaceAOTextureSampler.Bind(ParameterMap,TEXT("ScreenSpaceAOTextureSampler"));
	CustomDepthTexture.Bind(ParameterMap,TEXT("CustomDepthTexture"));
	CustomDepthTextureSampler.Bind(ParameterMap,TEXT("CustomDepthTextureSampler"));
}

bool IsDBufferEnabled();

template< typename ShaderRHIParamRef >
void FDeferredPixelShaderParameters::Set(FRHICommandList& RHICmdList, const ShaderRHIParamRef ShaderRHI, const FSceneView& View, ESceneRenderTargetsMode::Type TextureMode) const
{
	// This is needed on PC ES2 for SceneAlphaCopy, probably should be refactored for performance.
	SceneTextureParameters.Set(RHICmdList, ShaderRHI, View, TextureMode, SF_Point);

	// if() is purely an optimization and could be removed
	if(IsDBufferEnabled())
	{
		IPooledRenderTarget* DBufferA = GSceneRenderTargets.DBufferA ? GSceneRenderTargets.DBufferA : GSystemTextures.BlackDummy;
		IPooledRenderTarget* DBufferB = GSceneRenderTargets.DBufferB ? GSceneRenderTargets.DBufferB : GSystemTextures.BlackDummy;
		IPooledRenderTarget* DBufferC = GSceneRenderTargets.DBufferC ? GSceneRenderTargets.DBufferC : GSystemTextures.BlackDummy;

		// todo: optimize out when not needed
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferATexture, DBufferATextureSampler, TStaticSamplerState<>::GetRHI(), DBufferA->GetRenderTargetItem().ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferBTexture, DBufferBTextureSampler, TStaticSamplerState<>::GetRHI(), DBufferB->GetRenderTargetItem().ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferCTexture, DBufferCTextureSampler, TStaticSamplerState<>::GetRHI(), DBufferC->GetRenderTargetItem().ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferATextureMS, DBufferA->GetRenderTargetItem().TargetableTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferBTextureMS, DBufferB->GetRenderTargetItem().TargetableTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferCTextureMS, DBufferC->GetRenderTargetItem().TargetableTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferATextureNonMS, DBufferA->GetRenderTargetItem().ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferBTextureNonMS, DBufferB->GetRenderTargetItem().ShaderResourceTexture);
		SetTextureParameter(RHICmdList, ShaderRHI, DBufferCTextureNonMS, DBufferC->GetRenderTargetItem().ShaderResourceTexture);
	}

	const auto FeatureLevel = View.GetFeatureLevel();

	if (TextureMode == ESceneRenderTargetsMode::SetTextures && FeatureLevel >= ERHIFeatureLevel::SM3)
	{
		// if there is no ambient occlusion it's better to have white there
		IPooledRenderTarget* ScreenSpaceAO = GSceneRenderTargets.ScreenSpaceAO;
		if(!GSceneRenderTargets.bScreenSpaceAOIsValid)
		{
			ScreenSpaceAO = GSystemTextures.WhiteDummy;
		}

		// if there is no custom depth it's better to have the far distance there
		IPooledRenderTarget* CustomDepth = GSceneRenderTargets.bCustomDepthIsValid ? GSceneRenderTargets.CustomDepth : 0;
		if(!CustomDepth)
		{
			CustomDepth = GSystemTextures.BlackDummy;
		}

		if (FeatureLevel >= ERHIFeatureLevel::SM4)
		{
			if (GBufferResources.IsBound())
			{
				SetUniformBufferParameter(RHICmdList, ShaderRHI, GBufferResources, GSceneRenderTargets.GetGBufferResourcesUniformBuffer());
			}

			SetTextureParameter(RHICmdList, ShaderRHI, ScreenSpaceAOTexture, ScreenSpaceAOTextureSampler, TStaticSamplerState<>::GetRHI(), ScreenSpaceAO->GetRenderTargetItem().ShaderResourceTexture);
			SetTextureParameter(RHICmdList, ShaderRHI, ScreenSpaceAOTextureMS, ScreenSpaceAO->GetRenderTargetItem().TargetableTexture);
			SetTextureParameter(RHICmdList, ShaderRHI, ScreenSpaceAOTextureNonMS, ScreenSpaceAO->GetRenderTargetItem().ShaderResourceTexture);

			SetTextureParameter(RHICmdList, ShaderRHI, CustomDepthTexture, CustomDepthTextureSampler, TStaticSamplerState<>::GetRHI(), CustomDepth->GetRenderTargetItem().ShaderResourceTexture);
			SetTextureParameter(RHICmdList, ShaderRHI, CustomDepthTextureNonMS, CustomDepth->GetRenderTargetItem().ShaderResourceTexture);
		}
	}
	else if (TextureMode == ESceneRenderTargetsMode::DontSet ||
		TextureMode == ESceneRenderTargetsMode::DontSetIgnoreBoundByEditorCompositing)
	{
		// Verify that none of these are actually bound
		checkSlow(!GBufferResources.IsBound());
	}
}

#define IMPLEMENT_DEFERRED_PARAMETERS_SET( ShaderRHIParamRef ) \
	template void FDeferredPixelShaderParameters::Set< ShaderRHIParamRef >( \
		FRHICommandList& RHICmdList,				\
		const ShaderRHIParamRef ShaderRHI,			\
		const FSceneView& View,						\
		ESceneRenderTargetsMode::Type TextureMode	\
	) const;

IMPLEMENT_DEFERRED_PARAMETERS_SET( FVertexShaderRHIParamRef );
IMPLEMENT_DEFERRED_PARAMETERS_SET( FHullShaderRHIParamRef );
IMPLEMENT_DEFERRED_PARAMETERS_SET( FDomainShaderRHIParamRef );
IMPLEMENT_DEFERRED_PARAMETERS_SET( FGeometryShaderRHIParamRef );
IMPLEMENT_DEFERRED_PARAMETERS_SET( FPixelShaderRHIParamRef );
IMPLEMENT_DEFERRED_PARAMETERS_SET( FComputeShaderRHIParamRef );

FArchive& operator<<(FArchive& Ar,FDeferredPixelShaderParameters& Parameters)
{
	Ar << Parameters.SceneTextureParameters;

	Ar << Parameters.GBufferResources;
	Ar << Parameters.DBufferATextureMS;
	Ar << Parameters.DBufferBTextureMS;
	Ar << Parameters.DBufferCTextureMS;
	Ar << Parameters.ScreenSpaceAOTextureMS;
	Ar << Parameters.DBufferATextureNonMS;
	Ar << Parameters.DBufferBTextureNonMS;
	Ar << Parameters.DBufferCTextureNonMS;
	Ar << Parameters.ScreenSpaceAOTextureNonMS;
	Ar << Parameters.CustomDepthTextureNonMS;
	Ar << Parameters.DBufferATexture;
	Ar << Parameters.DBufferATextureSampler;
	Ar << Parameters.DBufferBTexture;
	Ar << Parameters.DBufferBTextureSampler;
	Ar << Parameters.DBufferCTexture;
	Ar << Parameters.DBufferCTextureSampler;
	Ar << Parameters.ScreenSpaceAOTexture;
	Ar << Parameters.ScreenSpaceAOTextureSampler;
	Ar << Parameters.CustomDepthTexture;
	Ar << Parameters.CustomDepthTextureSampler;

	return Ar;
}

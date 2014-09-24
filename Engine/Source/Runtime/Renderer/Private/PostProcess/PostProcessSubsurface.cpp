// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessSubsurface.cpp: Screenspace subsurface scattering implementation.
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneFilterRendering.h"
#include "PostProcessSubsurface.h"
#include "PostProcessing.h"
#include "SceneUtils.h"

ENGINE_API const IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);


static TAutoConsoleVariable<int32> CVarSSSSampleSet(
	TEXT("r.SSS.SampleSet"),
	2,
	TEXT("0: lowest quality\n")
	TEXT("1: medium quality\n")
	TEXT("2: high quality (default)"),
	ECVF_RenderThreadSafe  | ECVF_Scalability);


/**
 * Encapsulates the post processing ambient occlusion pixel shader.
 * @param SetupMode 0:without specular correction, 1: with specular correction, 2:visualize, 3: half res without specular correction, 4:half res with specular correction
 */
template <uint32 SetupMode>
class FPostProcessSubsurfaceSetupPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessSubsurfaceSetupPS , Global);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SETUP_MODE"), SetupMode);
	}

	/** Default constructor. */
	FPostProcessSubsurfaceSetupPS () {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter MiniFontTexture;

	/** Initialization constructor. */
	FPostProcessSubsurfaceSetupPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		DeferredParameters.Bind(Initializer.ParameterMap);
		MiniFontTexture.Bind(Initializer.ParameterMap, TEXT("MiniFontTexture"));
	}

	void SetParameters(const FRenderingCompositePassContext& Context)
	{
		const FFinalPostProcessSettings& Settings = Context.View.FinalPostProcessSettings;
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters(Context.RHICmdList, ShaderRHI, Context.View);
		PostprocessParameter.SetPS(ShaderRHI, Context, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());
		DeferredParameters.Set(Context.RHICmdList, ShaderRHI, Context.View);
		SetTextureParameter(Context.RHICmdList, ShaderRHI, MiniFontTexture, GEngine->MiniFontTexture ? GEngine->MiniFontTexture->Resource->TextureRHI : GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << DeferredParameters << MiniFontTexture;
		return bShaderHasOutdatedParameters;
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("PostProcessSubsurface");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("SetupPS");
	}
};

// #define avoids a lot of code duplication
#define VARIATION1(A) typedef FPostProcessSubsurfaceSetupPS<A> FPostProcessSubsurfaceSetupPS##A; \
	IMPLEMENT_SHADER_TYPE2(FPostProcessSubsurfaceSetupPS##A, SF_Pixel);

	VARIATION1(0) VARIATION1(1) VARIATION1(2) VARIATION1(3) VARIATION1(4)

#undef VARIATION1


template <uint32 SetupMode>
void SetSubsurfaceSetupShader(const FRenderingCompositePassContext& Context)
{
	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
	TShaderMapRef<FPostProcessSubsurfaceSetupPS<SetupMode> > PixelShader(Context.GetShaderMap());

	static FGlobalBoundShaderState BoundShaderState;

	SetGlobalBoundShaderState(Context.RHICmdList, Context.GetFeatureLevel(), BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

	PixelShader->SetParameters(Context);
	VertexShader->SetParameters(Context);
}

static TAutoConsoleVariable<int> CVarSubsurfaceQuality(
	TEXT("r.SubsurfaceQuality"),
	1,
	TEXT("Define the quality of the Screenspace subsurface scattering postprocess.\n")
	TEXT(" 0: low quality for speculars on subsurface materials\n")
	TEXT(" 1: higher quality as specular is separated before screenspace blurring (Only used if SceneColor has an alpha channel)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);


static bool DoSpecularCorrection()
{
	bool CVarState = CVarSubsurfaceQuality.GetValueOnRenderThread() > 0;

	int SceneColorFormat;
	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SceneColorFormat"));

		SceneColorFormat = CVar->GetInt();
	}

	// we need an alpha channel for this feature
	return CVarState && (SceneColorFormat >= 4);
}

FRCPassPostProcessSubsurfaceSetup::FRCPassPostProcessSubsurfaceSetup(bool bInVisualize, bool bInHalfRes)
	: bVisualize(bInVisualize)
	, bHalfRes(bInHalfRes)
{
	if (bVisualize)
	{
		GSceneRenderTargets.AdjustGBufferRefCount(1);
	}
}

void FRCPassPostProcessSubsurfaceSetup::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, SubsurfaceSetup, DEC_SCENE_ITEMS);

	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	if(!InputDesc)
	{
		// input is not hooked up correctly
		return;
	}

	const FSceneView& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	FIntPoint SrcSize = InputDesc->Extent;
	FIntPoint DestSize = PassOutputs[0].RenderTargetDesc.Extent;

	// e.g. 4 means the input texture is 4x smaller than the buffer size
	uint32 ScaleFactor = GSceneRenderTargets.GetBufferSizeXY().X / SrcSize.X;

	FIntRect SrcRect = View.ViewRect / ScaleFactor;
	FIntRect DestRect = bHalfRes ? FIntRect::DivideAndRoundUp(SrcRect, 2) : SrcRect;

	if(bHalfRes)
	{
		// upscale rectangle to not slightly scale
		SrcRect = DestRect * 2;
	}

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);

	// Set the view family's render target/viewport.
	SetRenderTarget(Context.RHICmdList, DestRenderTarget.TargetableTexture, FTextureRHIRef());

	// is optimized away if possible (RT size=view size, )
	Context.RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, DestRect);

	Context.SetViewportAndCallRHI(0, 0, 0.0f, DestSize.X, DestSize.Y, 1.0f );

	// set the state
	Context.RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());
	Context.RHICmdList.SetRasterizerState(TStaticRasterizerState<>::GetRHI());
	Context.RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	if (bVisualize)
	{
		SetSubsurfaceSetupShader<2>(Context);
	}
	else
	{
		// reconstruct specular and add it in final pass
		bool bSpecularCorrection = DoSpecularCorrection();

		if(bHalfRes)
		{
			if(bSpecularCorrection)
			{
				SetSubsurfaceSetupShader<4>(Context);
			}
			else
			{
				SetSubsurfaceSetupShader<3>(Context);
			}
		}
		else
		{
			if(bSpecularCorrection)
			{
				SetSubsurfaceSetupShader<1>(Context);
			}
			else
			{
				SetSubsurfaceSetupShader<0>(Context);
			}
		}
	}

	// Draw a quad mapping scene color to the view's render target
	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
	DrawRectangle(
		Context.RHICmdList,
		DestRect.Min.X, DestRect.Min.Y,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y,
		SrcRect.Width(), SrcRect.Height(),
		DestSize,
		SrcSize,
		*VertexShader,
		EDRF_UseTriangleOptimization);


	if (bVisualize)
	{
		// this is a helper class for FCanvas to be able to get screen size
		class FRenderTargetTemp : public FRenderTarget
		{
		public:
			const FSceneView& View;
			const FTexture2DRHIRef Texture;

			FRenderTargetTemp(const FSceneView& InView, const FTexture2DRHIRef InTexture)
				: View(InView), Texture(InTexture)
			{
			}
			virtual FIntPoint GetSizeXY() const
			{
				return View.ViewRect.Size();
			};
			virtual const FTexture2DRHIRef& GetRenderTargetTexture() const
			{
				return Texture;
			}
		} TempRenderTarget(View, (const FTexture2DRHIRef&)DestRenderTarget.TargetableTexture);

		FCanvas Canvas(&TempRenderTarget, NULL, ViewFamily.CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, Context.GetFeatureLevel());

		float X = 30;
		float Y = 28;
		const float YStep = 14;

		FString Line;

		Line = FString::Printf(TEXT("Visualize Screen Space Subsurface Scattering"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

		Y += YStep;

		uint32 Index = 0;
		while (GSubsufaceProfileTextureObject.GetEntryString(Index++, Line))
		{
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
		}

		Canvas.Flush_RenderThread(Context.RHICmdList);
	}

	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, DestRenderTarget.ShaderResourceTexture, false, FResolveParams());

	if (bVisualize)
	{
		GSceneRenderTargets.AdjustGBufferRefCount(-1);
	}
}

FPooledRenderTargetDesc FRCPassPostProcessSubsurfaceSetup::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GSceneRenderTargets.GetSceneColor()->GetDesc();

	Ret.Reset();
	Ret.DebugName = TEXT("SubsurfaceSetup");
	// we don't need alpha any more
	Ret.Format = PF_FloatRGB;

	if(bHalfRes)
	{
		Ret.Extent  = FIntPoint::DivideAndRoundUp(Ret.Extent, 2);

		Ret.Extent.X = FMath::Max(1, Ret.Extent.X);
		Ret.Extent.Y = FMath::Max(1, Ret.Extent.Y);
	}

	return Ret;
}


/** Encapsulates the post processing down sample pixel shader. */
// @param Method 0: horizontal, 1:vertical, 2:vertical and reconstruct specular
// @param SampleSet 0:low, 1:med, 2:high
template <uint32 Method, uint32 SampleSet>
class TPostProcessSubsurfacePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TPostProcessSubsurfacePS, Global);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform,OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SSS_METHOD"), Method);
		OutEnvironment.SetDefine(TEXT("SSS_SAMPLESET"), SampleSet);
	}

	/** Default constructor. */
	TPostProcessSubsurfacePS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderParameter SSSParams;
	FShaderResourceParameter SSProfilesTexture;
	FShaderResourceParameter SSProfilesTextureSampler;

	/** Initialization constructor. */
	TPostProcessSubsurfacePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		DeferredParameters.Bind(Initializer.ParameterMap);
		SSSParams.Bind(Initializer.ParameterMap, TEXT("SSSParams"));
		SSProfilesTexture.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		SSProfilesTextureSampler.Bind(Initializer.ParameterMap, TEXT("SSProfilesTextureSampler"));
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << DeferredParameters << SSSParams << SSProfilesTexture << SSProfilesTextureSampler;
		return bShaderHasOutdatedParameters;
	}

	void SetParameters(const FRenderingCompositePassContext& Context, float InRadius)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();

		FGlobalShader::SetParameters(Context.RHICmdList, ShaderRHI, Context.View);
		DeferredParameters.Set(Context.RHICmdList, ShaderRHI, Context.View);
		PostprocessParameter.SetPS(ShaderRHI, Context, TStaticSamplerState<SF_Bilinear,AM_Border,AM_Border,AM_Border>::GetRHI());

		{
			// from Separabale.usf: float distanceToProjectionWindow = 1.0 / tan(0.5 * radians(SSSS_FOVY))
			// can be extracted out of projection matrix

			float ScaleCorrectionX = Context.View.ViewRect.Width() / (float)GSceneRenderTargets.GetBufferSizeXY().X;
			float ScaleCorrectionY = Context.View.ViewRect.Height() / (float)GSceneRenderTargets.GetBufferSizeXY().Y;

			FVector4 ColorScale(InRadius, Context.View.ViewMatrices.ProjMatrix.M[0][0], ScaleCorrectionX, ScaleCorrectionY);
			SetShaderValue(Context.RHICmdList, ShaderRHI, SSSParams, ColorScale);
		}

		{
			const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT(Context.RHICmdList);

			check(PooledRT);

			const FSceneRenderTargetItem& Item = PooledRT->GetRenderTargetItem();

			SetTextureParameter(Context.RHICmdList, ShaderRHI, SSProfilesTexture, SSProfilesTextureSampler, TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI(), Item.ShaderResourceTexture);
		}
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("PostProcessSubsurface");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainPS");
	}
};

	// #define avoids a lot of code duplication
#define VARIATION1(A)		VARIATION2(A,0)			VARIATION2(A,1)			VARIATION2(A,2)
#define VARIATION2(A, B) typedef TPostProcessSubsurfacePS<A, B> TPostProcessSubsurfacePS##A##B; \
	IMPLEMENT_SHADER_TYPE2(TPostProcessSubsurfacePS##A##B, SF_Pixel);
	VARIATION1(0) VARIATION1(1) VARIATION1(2)
#undef VARIATION1
#undef VARIATION2


FRCPassPostProcessSubsurface::FRCPassPostProcessSubsurface(uint32 InPass, float InRadius, bool bInHalfRes)
	: Radius(InRadius)
	, Pass(InPass) 
	, bHalfRes(bInHalfRes) 
{
}

template <uint32 Method, uint32 SampleSet>
void SetSubsurfaceShader(const FRenderingCompositePassContext& Context, float InRadius)
{
	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());
	TShaderMapRef<TPostProcessSubsurfacePS<Method,SampleSet> > PixelShader(Context.GetShaderMap());

	static FGlobalBoundShaderState BoundShaderState;

	SetGlobalBoundShaderState(Context.RHICmdList, Context.GetFeatureLevel(), BoundShaderState, GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);

	PixelShader->SetParameters(Context, InRadius);
	VertexShader->SetParameters(Context);
}

// 0:horizontal, 1:vertical with spec correction, 2:vertical without spec correction
template <uint32 Method>
void SetSubsurfaceShaderSampleSet(const FRenderingCompositePassContext& Context, float InRadius, uint32 SampleSet)
{
	switch(SampleSet)
	{
		case 0: SetSubsurfaceShader<Method, 0>(Context, InRadius); break;
		case 1: SetSubsurfaceShader<Method, 1>(Context, InRadius); break;
		case 2: SetSubsurfaceShader<Method, 2>(Context, InRadius); break;

	default:
		check(0);
	}
}

void FRCPassPostProcessSubsurface::Process(FRenderingCompositePassContext& Context)
{
	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input1);

	check(InputDesc);

	{
		const IPooledRenderTarget* PooledRT = GetSubsufaceProfileTexture_RT(Context.RHICmdList);

		check(PooledRT);

		// for debugging
		GRenderTargetPool.VisualizeTexture.SetCheckPoint(Context.RHICmdList, PooledRT);
	}

	const FSceneView& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);

	FIntPoint SrcSize = InputDesc->Extent;
	FIntPoint DestSize = (Pass == 0) ? PassOutputs[0].RenderTargetDesc.Extent : GSceneRenderTargets.GetBufferSizeXY();

	// e.g. 4 means the input texture is 4x smaller than the buffer size
	uint32 ScaleFactor = GSceneRenderTargets.GetBufferSizeXY().X / SrcSize.X;

	FIntRect SrcRect = View.ViewRect / ScaleFactor;
	FIntRect DestRect = bHalfRes ? SrcRect : View.ViewRect;

	TRefCountPtr<IPooledRenderTarget> NewSceneColor;

	const FSceneRenderTargetItem* DestRenderTarget;
	{
		if(Pass == 0)
		{
			DestRenderTarget = &PassOutputs[0].RequestSurface(Context);
		}
		else
		{
			// in that case we directly render to the HDR scene color
			GRenderTargetPool.FindFreeElement(GSceneRenderTargets.GetSceneColor()->GetDesc(), NewSceneColor, TEXT("SceneColor"));
			DestRenderTarget = &NewSceneColor->GetRenderTargetItem();
		}

		check(DestRenderTarget);
	}

	// Set the view family's render target/viewport.
	SetRenderTarget(Context.RHICmdList, DestRenderTarget->TargetableTexture, FTextureRHIRef());

	// is optimized away if possible (RT size=view size, )
	Context.RHICmdList.Clear(true, FLinearColor(0, 0, 0, 0), false, 1.0f, false, 0, DestRect);

	Context.SetViewportAndCallRHI(0, 0, 0.0f, DestSize.X, DestSize.Y, 1.0f );

	Context.RHICmdList.SetBlendState(TStaticBlendState<>::GetRHI());
	Context.RHICmdList.SetRasterizerState(TStaticRasterizerState<>::GetRHI());
	Context.RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	TShaderMapRef<FPostProcessVS> VertexShader(Context.GetShaderMap());

	const TCHAR* PassNames[] = { TEXT("X"), TEXT("Y") };
	check(Pass < sizeof(PassNames) / sizeof(PassNames[0]));

	SCOPED_DRAW_EVENTF(Context.RHICmdList, SubsurfacePass, DEC_SCENE_ITEMS, PassNames[Pass]);

	uint32 SampleSet = FMath::Clamp(CVarSSSSampleSet.GetValueOnRenderThread(), 0, 2);

	if (Pass == 0)
	{
		SetSubsurfaceShaderSampleSet<0>(Context, Radius, SampleSet);
	}
	else
	{
		check(Pass == 1);

		if(DoSpecularCorrection())
		{
			// reconstruct specular and add it in final pass
			SetSubsurfaceShaderSampleSet<2>(Context, Radius, SampleSet);
		}
		else
		{
			SetSubsurfaceShaderSampleSet<1>(Context, Radius, SampleSet);
		}
	}

	DrawRectangle(
		Context.RHICmdList,
		DestRect.Min.X, DestRect.Min.Y,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y,
		SrcRect.Width(), SrcRect.Height(),
		DestSize,
		SrcSize,
		*VertexShader,
		EDRF_UseTriangleOptimization);

	Context.RHICmdList.CopyToResolveTarget(DestRenderTarget->TargetableTexture, DestRenderTarget->ShaderResourceTexture, false, FResolveParams());

	if(Pass != 0)
	{
		GSceneRenderTargets.SetSceneColor(NewSceneColor);
	}
}


FPooledRenderTargetDesc FRCPassPostProcessSubsurface::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret;

	if(Pass != 0)
	{
		// in that case we directly render to the HDR scene color
		return Ret;
	}

	Ret = PassInputs[1].GetOutput()->RenderTargetDesc;

	Ret.Reset();
	Ret.DebugName = (Pass == 0) ? TEXT("SubsurfaceTemp") : TEXT("SceneColor");
	// the setup was done in half res but the actual sampling happens in full resolution
	Ret.Extent = GSceneRenderTargets.GetBufferSizeXY();

	if(bHalfRes)
	{
		Ret.Extent  = FIntPoint::DivideAndRoundUp(Ret.Extent, 2);

		Ret.Extent.X = FMath::Max(1, Ret.Extent.X);
		Ret.Extent.Y = FMath::Max(1, Ret.Extent.Y);
	}

	return Ret;
}
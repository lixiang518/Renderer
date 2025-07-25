// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneProxies/SkyLightSceneProxy.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LightSceneProxy.h"
#include "GPUScene.h"
#include "Rendering/NaniteResources.h"
#include "Nanite/Nanite.h"
#include "Nanite/NaniteShading.h"
#include "PixelShaderUtils.h"
#include "Lumen.h"
#include "LumenMeshCards.h"
#include "LumenSurfaceCacheFeedback.h"
#include "LumenSceneLighting.h"
#include "LumenSceneCardCapture.h"
#include "LumenTracingUtils.h"
#include "GlobalDistanceField.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "HAL/LowLevelMemStats.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "StaticMeshBatch.h"
#include "LumenReflections.h"
#include "RayTracedTranslucency.h"
#include "LumenRadiosity.h"
#include "StereoRendering.h"
#include "SkyAtmosphereRendering.h"
#include "ComponentRecreateRenderStateContext.h"
#include "VT/VirtualTextureFeedbackResource.h"

int32 GLumenFastCameraMode = 0;
FAutoConsoleVariableRef CVarLumenFastCameraMode(
	TEXT("r.LumenScene.FastCameraMode"),
	GLumenFastCameraMode,
	TEXT("Whether to update the Lumen Scene for fast camera movement - lower quality, faster updates so lighting can keep up with the camera."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneParallelUpdate = 1;
FAutoConsoleVariableRef CVarLumenSceneParallelUpdate(
	TEXT("r.LumenScene.ParallelUpdate"),
	GLumenSceneParallelUpdate,
	TEXT("Whether to run the Lumen Scene update in parallel."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScenePrimitivesPerTask = 128;
FAutoConsoleVariableRef CVarLumenScenePrimitivePerTask(
	TEXT("r.LumenScene.PrimitivesPerTask"),
	GLumenScenePrimitivesPerTask,
	TEXT("How many primitives to process per single surface cache update task."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneMeshCardsPerTask = 128;
FAutoConsoleVariableRef CVarLumenSceneMeshCardsPerTask(
	TEXT("r.LumenScene.MeshCardsPerTask"),
	GLumenSceneMeshCardsPerTask,
	TEXT("How many mesh cards to process per single surface cache update task."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSurfaceCacheFreeze = 0;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheFreeze(
	TEXT("r.LumenScene.SurfaceCache.Freeze"),
	GLumenSurfaceCacheFreeze,
	TEXT("Freeze surface cache updates for debugging.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSurfaceCacheFreezeUpdateFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheFreezeUpdateFrame(
	TEXT("r.LumenScene.SurfaceCache.FreezeUpdateFrame"),
	GLumenSurfaceCacheFreezeUpdateFrame,
	TEXT("Keep updating the same subset of surface cache for debugging and profiling.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneSurfaceCacheReset = 0;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheReset(
	TEXT("r.LumenScene.SurfaceCache.Reset"),
	GLumenSceneSurfaceCacheReset,
	TEXT("Reset all atlases and captured cards.\n"),	
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneSurfaceCacheResetEveryNthFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheResetEveryNthFrame(
	TEXT("r.LumenScene.SurfaceCache.ResetEveryNthFrame"),
	GLumenSceneSurfaceCacheResetEveryNthFrame,
	TEXT("Continuously reset all atlases and captured cards every N-th frame.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneCardCapturesPerFrame = 300;
FAutoConsoleVariableRef CVarLumenSceneCardCapturesPerFrame(
	TEXT("r.LumenScene.SurfaceCache.CardCapturesPerFrame"),
	GLumenSceneCardCapturesPerFrame,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneCardCaptureFactor = 64;
FAutoConsoleVariableRef CVarLumenSceneCardCaptureFactor(
	TEXT("r.LumenScene.SurfaceCache.CardCaptureFactor"),
	GLumenSceneCardCaptureFactor,
	TEXT("Controls how many texels can be captured per frame. Texels = SurfaceCacheTexels / Factor."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarLumenSceneSurfaceCacheRemovesPerFrame(
	TEXT("r.LumenScene.SurfaceCache.RemovesPerFrame"),
	512,
	TEXT("How many mesh cards removes can be done per frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

TAutoConsoleVariable<float> CVarLumenSceneCardCaptureRefreshFraction(
	TEXT("r.LumenScene.SurfaceCache.CardCaptureRefreshFraction"),
	0.125f,
	TEXT("Fraction of card capture budget allowed to be spent on re-capturing existing pages in order to refresh surface cache materials.\n")
	TEXT("0 disables card refresh."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarLumenSceneCardCaptureEnableInvalidation(
	TEXT("r.LumenScene.SurfaceCache.CardCaptureEnableInvalidation"),
	1,
	TEXT("Whether to enable manual card recapture through InvalidateSurfaceCacheForPrimitive().\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneCardFixedDebugResolution = -1;
FAutoConsoleVariableRef CVarLumenSceneCardFixedDebugResolution(
	TEXT("r.LumenScene.SurfaceCache.CardFixedDebugResolution"),
	GLumenSceneCardFixedDebugResolution,
	TEXT("Lumen card resolution"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenSceneCardMaxTexelDensity = .2f;
FAutoConsoleVariableRef CVarLumenSceneCardMaxTexelDensity(
	TEXT("r.LumenScene.SurfaceCache.CardMaxTexelDensity"),
	GLumenSceneCardMaxTexelDensity,
	TEXT("Lumen card texels per world space distance"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneCardMaxResolution = 512;
FAutoConsoleVariableRef CVarLumenSceneCardMaxResolution(
	TEXT("r.LumenScene.SurfaceCache.CardMaxResolution"),
	GLumenSceneCardMaxResolution,
	TEXT("Maximum card resolution in Lumen Scene"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GSurfaceCacheNumFramesToKeepUnusedPages = 256;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheNumFramesToKeepUnusedPages(
	TEXT("r.LumenScene.SurfaceCache.NumFramesToKeepUnusedPages"),
	GSurfaceCacheNumFramesToKeepUnusedPages,
	TEXT("Num frames to keep unused pages in surface cache."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneForceEvictHiResPages = 0;
FAutoConsoleVariableRef CVarLumenSceneForceEvictHiResPages(
	TEXT("r.LumenScene.SurfaceCache.ForceEvictHiResPages"),
	GLumenSceneForceEvictHiResPages,
	TEXT("Evict all optional hi-res surface cache pages."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneRecaptureLumenSceneEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenGIRecaptureLumenSceneEveryFrame(
	TEXT("r.LumenScene.SurfaceCache.RecaptureEveryFrame"),
	GLumenSceneRecaptureLumenSceneEveryFrame,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneSurfaceCacheLogUpdates = 0;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheLogUpdates(
	TEXT("r.LumenScene.SurfaceCache.LogUpdates"),
	GLumenSceneSurfaceCacheLogUpdates,
	TEXT("Whether to log Lumen surface cache updates.\n")
	TEXT("2 - will log mesh names."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneSurfaceCacheResampleLighting = 1;
FAutoConsoleVariableRef CVarLumenSceneSurfaceCacheResampleLighting(
	TEXT("r.LumenScene.SurfaceCache.ResampleLighting"),
	GLumenSceneSurfaceCacheResampleLighting,
	TEXT("Whether to resample card lighting when cards are reallocated.  This is needed for Radiosity temporal accumulation but can be disabled for debugging."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneSurfaceCacheNaniteMultiView(
	TEXT("r.LumenScene.SurfaceCache.Nanite.MultiView"),
	1,
	TEXT("Toggle multi view Lumen Nanite Card capture for debugging."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
		{
			Lumen::DebugResetSurfaceCache();
		}),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarLumenSceneSurfaceCacheNaniteAsyncRasterization(
	TEXT("r.LumenScene.SurfaceCache.Nanite.AsyncRasterization"),
	0,
	TEXT("Whether to use Nanite async rasterization for Mesh Card capture."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarLumenScenePropagateGlobalLightingChange(
	TEXT("r.LumenScene.PropagateGlobalLightingChange"),
	1,
	TEXT("Whether to detect big scene lighting changes and speedup Lumen update for those frames."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneGPUDrivenUpdate(
	TEXT("r.LumenScene.GPUDrivenUpdate"),
	0,
	TEXT("Whether to use GPU to update Lumen Scene. Work in progress."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneViewOriginDistanceThreshold(
	TEXT("r.LumenScene.ViewOriginDistanceThreshold"),
	100,
	TEXT("Distance threshold below which views' origins are considered identical. Used for streaming request with multiple views. Default 100 (= 1 meter)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneUploadEveryFrame(
	TEXT("r.LumenScene.UploadEveryFrame"),
	0,
	TEXT("Whether to upload the entire Lumen Scene's data every frame. Useful for debugging."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneSurfaceCacheAllowCardDownsampleFromSelf(
	TEXT("r.LumenScene.SurfaceCache.AllowCardDownsampleFromSelf"),
	1,
	TEXT("Whether cards are allowed to downsample from self instead of recapture when lowering resolutions."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneSurfaceCacheAllowCardSharing(
	TEXT("r.LumenScene.SurfaceCache.AllowCardSharing"),
	1,
	TEXT("Whether to allow sharing Lumen cards between instances."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarLumenSceneSurfaceCacheDetectCardSharingCompatibility(
	TEXT("r.LumenScene.SurfaceCache.DetectCardSharingCompatibility"),
	1,
	TEXT("Whether to auto detect card sharing compatibility. Currently, a component is incompatible if it uses any material that has any of the following nodes: ")
	TEXT("PerInstanceRandom, PerInstanceCustomData, WorldPosition, and ActorPositionWS."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* CVar) { FGlobalComponentRecreateRenderStateContext Context; }),
	ECVF_RenderThreadSafe
);

#if ENABLE_LOW_LEVEL_MEM_TRACKER
DECLARE_LLM_MEMORY_STAT(TEXT("Lumen"), STAT_LumenLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Lumen"), STAT_LumenSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(Lumen, NAME_None, NAME_None, GET_STATFNAME(STAT_LumenLLM), GET_STATFNAME(STAT_LumenSummaryLLM));
#endif // ENABLE_LOW_LEVEL_MEM_TRACKER

extern int32 GAllowLumenDiffuseIndirect;
extern int32 GAllowLumenReflections;

void Lumen::DebugResetSurfaceCache()
{
	GLumenSceneSurfaceCacheReset = 1;
}

bool Lumen::IsSurfaceCacheFrozen()
{
	return GLumenSurfaceCacheFreeze != 0;
}

bool Lumen::IsSurfaceCacheUpdateFrameFrozen()
{
	return GLumenSurfaceCacheFreeze != 0 || GLumenSurfaceCacheFreezeUpdateFrame != 0;
}

int32 GetCardMaxResolution()
{
	if (GLumenFastCameraMode)
	{
		return GLumenSceneCardMaxResolution / 2;
	}

	return GLumenSceneCardMaxResolution;
}

int32 GetMaxLumenSceneCardCapturesPerFrame()
{
	return FMath::Max(GLumenSceneCardCapturesPerFrame * (GLumenFastCameraMode ? 2 : 1), 0);
}

namespace LumenScene
{
	bool AllowSurfaceCacheCardSharing()
	{
		return CVarLumenSceneSurfaceCacheAllowCardSharing.GetValueOnRenderThread() != 0;
	}

	int32 GetMaxMeshCardsToAddPerFrame()
	{
		return 2 * GetMaxLumenSceneCardCapturesPerFrame();
	}

	int32 GetMaxMeshCardsRemovesPerFrame()
	{
		return FMath::Max(CVarLumenSceneSurfaceCacheRemovesPerFrame.GetValueOnRenderThread(), 0);
	}
}

int32 GetMaxTileCapturesPerFrame()
{
	if (Lumen::IsSurfaceCacheFrozen())
	{
		return 0;
	}

	if (GLumenSceneRecaptureLumenSceneEveryFrame != 0)
	{
		return INT32_MAX;
	}

	return GetMaxLumenSceneCardCapturesPerFrame();
}

uint32 FLumenSceneData::GetSurfaceCacheUpdateFrameIndex() const
{
	return SurfaceCacheUpdateFrameIndex;
}

void FLumenSceneData::IncrementSurfaceCacheUpdateFrameIndex()
{
	if (!Lumen::IsSurfaceCacheUpdateFrameFrozen())
	{
		++SurfaceCacheUpdateFrameIndex;
		if (SurfaceCacheUpdateFrameIndex == 0)
		{
			++SurfaceCacheUpdateFrameIndex;
		}
	}
}

void AddLumenStreamingViewOrigins(const FSceneViewFamily& ViewFamily, TArray<FVector, TInlineAllocator<LUMEN_MAX_VIEWS>>& OutOrigins)
{
	// Add streaming view origins, only if there are futher apart than existing origins
	const float DistanceThreshold = CVarLumenSceneViewOriginDistanceThreshold.GetValueOnRenderThread();
	const float SqDistanceThreshold = DistanceThreshold * DistanceThreshold;
	for (const FVector& StreamViewOrigin : ViewFamily.StreamingViewOrigins)
	{
		bool bAddOrigin = true;
		for (const FVector& Origin : OutOrigins)
		{
			if (FVector::DistSquared(StreamViewOrigin, Origin) < SqDistanceThreshold)
			{
				bAddOrigin = false;
				break;
			}
		}

		if (bAddOrigin && OutOrigins.Num() < LUMEN_MAX_VIEWS)
		{
			OutOrigins.Add(StreamViewOrigin);
		}
	}
}

DECLARE_GPU_STAT(LumenSceneUpdate);
DECLARE_GPU_STAT(UpdateLumenSceneBuffers);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FLumenCardPassUniformParameters, "LumenCardPass", SceneTextures);

bool LumenScene::HasPrimitiveNaniteMeshBatches(const FPrimitiveSceneProxy* Proxy)
{
	return Proxy
		&& Proxy->ShouldRenderInMainPass()
		&& (Proxy->AffectsDynamicIndirectLighting()
			|| (Proxy->IsLandscapeNaniteProxy() && Proxy->GetSourceLandscapeComponentIds().Num() > 0));
}

struct FMeshCardsAdd
{
	int32 PrimitiveGroupIndex;
	float DistanceSquared;
};

struct FMeshCardsRemove
{
	int32 PrimitiveGroupIndex;
};

struct FInstanceRange
{
	int32 Offset;
	int32 Num;

	FInstanceRange(int32 InOffset, int32 InNum)
		: Offset(InOffset)
		, Num(InNum)
	{}
};

struct FCardAllocationOutput
{
	bool bVisible = false;
	int32 ResLevel = -1;
};

// Loop over Lumen primitive culling infos and output FMeshCards adds, removes, and instance culling ranges
struct FLumenSurfaceCacheCullPrimitivesTask
{
public:
	FLumenSurfaceCacheCullPrimitivesTask(
		TSparseArray<FLumenPrimitiveGroupCullingInfo>& InPrimitiveCullingInfos,
		const TArray<FVector, TInlineAllocator<2>>& InViewOrigins,
		bool bInOrthographicCamera,
		float InLumenSceneDetail,
		float InMaxDistanceFromCamera,
		int32 InFirstCullingInfoIndex,
		int32 InNumCullingInfosPerPacket,
		bool  InAddTranslucentToCache)
		: PrimitiveCullingInfos(InPrimitiveCullingInfos)
		, ViewOrigins(InViewOrigins)
		, bOrthographicCamera(bInOrthographicCamera)
		, FirstCullingInfoIndex(InFirstCullingInfoIndex)
		, NumCullingInfosPerPacket(InNumCullingInfosPerPacket)
		, LumenSceneDetail(InLumenSceneDetail)
		, MaxDistanceFromCameraSq(InMaxDistanceFromCamera * InMaxDistanceFromCamera)
		, TexelDensityScale(LumenScene::GetCardTexelDensity())
		, MinCardResolution(FMath::Clamp(FMath::RoundToInt(LumenScene::GetCardMinResolution(bInOrthographicCamera) / LumenSceneDetail), 1, 1024))
		, FarFieldCardMaxDistanceSq(LumenScene::GetFarFieldCardMaxDistance() * LumenScene::GetFarFieldCardMaxDistance())
		, FarFieldCardTexelDensity(LumenScene::GetFarFieldCardTexelDensity())
		, bAddTranslucentToCache(InAddTranslucentToCache)
	{
	}

	// Output
	TArray<FMeshCardsAdd> MeshCardsAdds;
	TArray<FMeshCardsRemove> MeshCardsRemoves;
	TArray<FInstanceRange> InstanceCullingRanges;

	void AnyThreadTask()
	{
		const int32 LastCullingInfoIndex = FMath::Min(FirstCullingInfoIndex + NumCullingInfosPerPacket, PrimitiveCullingInfos.GetMaxIndex());

		for (int32 CullingInfoIndex = FirstCullingInfoIndex; CullingInfoIndex < LastCullingInfoIndex; ++CullingInfoIndex)
		{
			if (PrimitiveCullingInfos.IsAllocated(CullingInfoIndex))
			{
				FLumenPrimitiveGroupCullingInfo& CullingInfo = PrimitiveCullingInfos[CullingInfoIndex];

				// Rough card min resolution test
				float DistanceSquared = FLT_MAX; // LWC_TODO

				for (const FVector& ViewOrigin : ViewOrigins)
				{
					DistanceSquared = FMath::Min(DistanceSquared, ComputeSquaredDistanceFromBoxToPoint(FVector(CullingInfo.WorldSpaceBoundingBox.Min), FVector(CullingInfo.WorldSpaceBoundingBox.Max), ViewOrigin)); // LWC_TODO
				}

				const float CardMaxDistanceSq = CullingInfo.bFarField ? FarFieldCardMaxDistanceSq : MaxDistanceFromCameraSq;
				
				if (CullingInfo.NumInstances > 0)
				{
					const bool bWasVisible = CullingInfo.bVisible;
					CullingInfo.bVisible = DistanceSquared <= CardMaxDistanceSq;

					// May need to hide instances if was visible. May need to show instances if is visible
					if (bWasVisible || CullingInfo.bVisible)
					{
						InstanceCullingRanges.Add(FInstanceRange(CullingInfo.InstanceCullingInfoOffset, CullingInfo.NumInstances));
					}
				}
				else
				{
					const float MaxCardExtent = CullingInfo.WorldSpaceBoundingBox.GetExtent().GetMax();
					float MaxCardResolution;

					// Far field cards have constant resolution over entire range
					if (CullingInfo.bFarField)
					{
						MaxCardResolution = MaxCardExtent * FarFieldCardTexelDensity;
					}
					else
					{
						MaxCardResolution = (TexelDensityScale * MaxCardExtent) / FMath::Sqrt(FMath::Max(DistanceSquared, 1.0f)) + 0.01f;
					}

					if (DistanceSquared <= CardMaxDistanceSq && MaxCardResolution >= (CullingInfo.bEmissiveLightSource ? 1.0f : MinCardResolution) && (CullingInfo.bOpaqueOrMasked || bAddTranslucentToCache))
					{
						if (!CullingInfo.bVisible && CullingInfo.bValidMeshCards)
						{
							FMeshCardsAdd Add;
							Add.PrimitiveGroupIndex = CullingInfo.PrimitiveGroupIndex;
							Add.DistanceSquared = DistanceSquared;
							MeshCardsAdds.Add(Add);
						}
					}
					else if (CullingInfo.bVisible)
					{
						FMeshCardsRemove Remove;
						Remove.PrimitiveGroupIndex = CullingInfo.PrimitiveGroupIndex;
						MeshCardsRemoves.Add(Remove);
					}
				}
			}
		}
	}

	TSparseArray<FLumenPrimitiveGroupCullingInfo>& PrimitiveCullingInfos;
	TArray<FVector, TInlineAllocator<2>> ViewOrigins;
	bool bOrthographicCamera;
	int32 FirstCullingInfoIndex;
	int32 NumCullingInfosPerPacket;
	float LumenSceneDetail;
	float MaxDistanceFromCameraSq;
	float TexelDensityScale;

	const int32 MinCardResolution;
	const float FarFieldCardMaxDistanceSq;
	const float FarFieldCardTexelDensity;

	const bool bAddTranslucentToCache;
};

// Loop over selected instances and output FMeshCards adds and removes
struct FLumenSurfaceCacheCullInstancesTask
{
public:
	FLumenSurfaceCacheCullInstancesTask(
		const TSparseSpanArray<FLumenPrimitiveGroupCullingInfo>& InInstanceCullingInfos,
		TConstArrayView<FInstanceRange> InInstanceRanges,
		TConstArrayView<int32> InRebasedRangeOffsets,
		const TArray<FVector, TInlineAllocator<2>>& InViewOrigins,
		bool bInOrthographicCamera,
		float InLumenSceneDetail,
		float InMaxDistanceFromCamera,
		int32 InFirstCullingInfoIndex,
		int32 InNumCullingInfosPerPacket,
		bool  InAddTranslucentToCache)
		: InstanceCullingInfos(InInstanceCullingInfos)
		, InstanceRanges(InInstanceRanges)
		, RebasedRangeOffsets(InRebasedRangeOffsets)
		, ViewOrigins(InViewOrigins)
		, bOrthographicCamera(bInOrthographicCamera)
		, FirstCullingInfoIndex(InFirstCullingInfoIndex)
		, NumCullingInfosPerPacket(InNumCullingInfosPerPacket)
		, LumenSceneDetail(InLumenSceneDetail)
		, MaxDistanceFromCameraSq(InMaxDistanceFromCamera * InMaxDistanceFromCamera)
		, TexelDensityScale(LumenScene::GetCardTexelDensity())
		, MinCardResolution(FMath::Clamp(FMath::RoundToInt(LumenScene::GetCardMinResolution(bInOrthographicCamera) / LumenSceneDetail), 1, 1024))
		, FarFieldCardMaxDistanceSq(LumenScene::GetFarFieldCardMaxDistance()* LumenScene::GetFarFieldCardMaxDistance())
		, FarFieldCardTexelDensity(LumenScene::GetFarFieldCardTexelDensity())
		, bAddTranslucentToCache(InAddTranslucentToCache)
	{
	}

	// Output
	TArray<FMeshCardsAdd> MeshCardsAdds;
	TArray<FMeshCardsRemove> MeshCardsRemoves;

	void AnyThreadTask()
	{
		int32 InstanceRangeIndex = Algo::UpperBound(RebasedRangeOffsets, FirstCullingInfoIndex) - 1;

		if (InstanceRangeIndex >= RebasedRangeOffsets.Num() - 1)
		{
			return;
		}

		const int32 LastCullingInfoIndex = FMath::Min(FirstCullingInfoIndex + NumCullingInfosPerPacket, RebasedRangeOffsets.Last());
		int32 NumCullingInfos = LastCullingInfoIndex - FirstCullingInfoIndex;
		int32 CullingInfoIndex = FirstCullingInfoIndex - RebasedRangeOffsets[InstanceRangeIndex] + InstanceRanges[InstanceRangeIndex].Offset;

		while (NumCullingInfos > 0)
		{
			const FLumenPrimitiveGroupCullingInfo& CullingInfo = InstanceCullingInfos[CullingInfoIndex];
			check(InstanceCullingInfos.IsAllocated(CullingInfoIndex) && CullingInfo.NumInstances == 0);

			// Rough card min resolution test
			float DistanceSquared = FLT_MAX; // LWC_TODO

			for (const FVector& ViewOrigin : ViewOrigins)
			{
				DistanceSquared = FMath::Min(DistanceSquared, ComputeSquaredDistanceFromBoxToPoint(FVector(CullingInfo.WorldSpaceBoundingBox.Min), FVector(CullingInfo.WorldSpaceBoundingBox.Max), ViewOrigin)); // LWC_TODO
			}

			const float MaxCardExtent = CullingInfo.WorldSpaceBoundingBox.GetExtent().GetMax();
			float CardMaxDistanceSq;
			float MaxCardResolution;

			// Far field cards have constant resolution over entire range
			if (CullingInfo.bFarField)
			{
				CardMaxDistanceSq = FarFieldCardMaxDistanceSq;
				MaxCardResolution = MaxCardExtent * FarFieldCardTexelDensity;
			}
			else
			{
				CardMaxDistanceSq = MaxDistanceFromCameraSq;
				MaxCardResolution = (TexelDensityScale * MaxCardExtent) / FMath::Sqrt(FMath::Max(DistanceSquared, 1.0f)) + 0.01f;
			}

			if (DistanceSquared <= CardMaxDistanceSq && MaxCardResolution >= (CullingInfo.bEmissiveLightSource ? 1.0f : MinCardResolution) && (CullingInfo.bOpaqueOrMasked || bAddTranslucentToCache))
			{
				if (!CullingInfo.bVisible && CullingInfo.bValidMeshCards)
				{
					FMeshCardsAdd Add;
					Add.PrimitiveGroupIndex = CullingInfo.PrimitiveGroupIndex;
					Add.DistanceSquared = DistanceSquared;
					MeshCardsAdds.Add(Add);
				}
			}
			else if (CullingInfo.bVisible)
			{
				FMeshCardsRemove Remove;
				Remove.PrimitiveGroupIndex = CullingInfo.PrimitiveGroupIndex;
				MeshCardsRemoves.Add(Remove);
			}

			--NumCullingInfos;
			++CullingInfoIndex;
			
			if (NumCullingInfos > 0 && CullingInfoIndex >= InstanceRanges[InstanceRangeIndex].Offset + InstanceRanges[InstanceRangeIndex].Num)
			{
				++InstanceRangeIndex;
				CullingInfoIndex = InstanceRanges[InstanceRangeIndex].Offset;
			}
		}
	}

	const TSparseSpanArray<FLumenPrimitiveGroupCullingInfo>& InstanceCullingInfos;
	TConstArrayView<FInstanceRange> InstanceRanges;
	TConstArrayView<int32> RebasedRangeOffsets;
	TArray<FVector, TInlineAllocator<2>> ViewOrigins;
	bool bOrthographicCamera;
	int32 FirstCullingInfoIndex;
	int32 NumCullingInfosPerPacket;
	float LumenSceneDetail;
	float MaxDistanceFromCameraSq;
	float TexelDensityScale;

	const int32 MinCardResolution;
	const float FarFieldCardMaxDistanceSq;
	const float FarFieldCardTexelDensity;

	const bool bAddTranslucentToCache;
};

struct FSurfaceCacheRemove
{
public:
	int32 LumenCardIndex;
};

// Loop over Lumen mesh cards and output card updates
struct FLumenSurfaceCacheUpdateMeshCardsTask
{
public:
	FLumenSurfaceCacheUpdateMeshCardsTask(
		const TSparseSpanArray<FLumenMeshCards>& InLumenMeshCards,
		const TSparseSpanArray<FLumenCard>& InLumenCards,
		const TArray<FVector, TInlineAllocator<2>>& InViewOrigins,
		bool bInOrthographicCamera,
		float InSurfaceCacheResolution,
		float InLumenSceneDetail,
		float InMaxDistanceFromCamera,
		int32 InFirstMeshCardsIndex,
		int32 InNumMeshCardsPerPacket)
		: LumenMeshCards(InLumenMeshCards)
		, LumenCards(InLumenCards)
		, ViewOrigins(InViewOrigins)
		, bOrthographicCamera(bInOrthographicCamera)
		, LumenSceneDetail(InLumenSceneDetail)
		, FirstMeshCardsIndex(InFirstMeshCardsIndex)
		, NumMeshCardsPerPacket(InNumMeshCardsPerPacket)
		, MaxDistanceFromCamera(InMaxDistanceFromCamera)
		, TexelDensityScale(LumenScene::GetCardTexelDensity() * InSurfaceCacheResolution)
		, MaxTexelDensity(GLumenSceneCardMaxTexelDensity)
		, MinCardResolution(FMath::Clamp(FMath::RoundToInt(LumenScene::GetCardMinResolution(bInOrthographicCamera) / LumenSceneDetail), 1, 1024))
		, FarFieldCardMaxDistance(LumenScene::GetFarFieldCardMaxDistance())
		, FarFieldCardTexelDensity(LumenScene::GetFarFieldCardTexelDensity())
	{
	}

	// Output
	TArray<FSurfaceCacheRequest> SurfaceCacheRequests;
	TArray<int32> CardsToHide;
	int32 Histogram[Lumen::NumDistanceBuckets] { 0 };

	void AnyThreadTask()
	{
		QUICK_SCOPE_CYCLE_COUNTER(LumenSurfaceCacheUpdateMeshCardsTask)

		const int32 LastLumenMeshCardsIndex = FMath::Min(FirstMeshCardsIndex + NumMeshCardsPerPacket, LumenMeshCards.Num());
		const int32 CardMaxResolution = GetCardMaxResolution();

		for (int32 MeshCardsIndex = FirstMeshCardsIndex; MeshCardsIndex < LastLumenMeshCardsIndex; ++MeshCardsIndex)
		{
			if (LumenMeshCards.IsAllocated(MeshCardsIndex))
			{
				const FLumenMeshCards& MeshCardsInstance = LumenMeshCards[MeshCardsIndex];

				const int32 MinCardResolutionForMeshCards = MeshCardsInstance.bEmissiveLightSource ? 1 : MinCardResolution;

				for (uint32 CardIndex = MeshCardsInstance.FirstCardIndex; CardIndex < MeshCardsInstance.FirstCardIndex + MeshCardsInstance.NumCards; ++CardIndex)
				{
					const FLumenCard& LumenCard = LumenCards[CardIndex];

					float ViewerDistance = FLT_MAX; // LWC_TODO

					for (const FVector& ViewOrigin : ViewOrigins)
					{
						ViewerDistance = FMath::Min(ViewerDistance, FMath::Max(FMath::Sqrt(LumenCard.WorldOBB.ComputeSquaredDistanceToPoint(ViewOrigin)), 100.0f));
					}

					// Compute resolution based on its largest extent
					float MaxExtent = FMath::Max(LumenCard.WorldOBB.Extent.X, LumenCard.WorldOBB.Extent.Y);
					float CardMaxDistance;
					float MaxProjectedSize;

					// Far field cards have constant resolution over entire range
					if (MeshCardsInstance.bFarField)
					{
						CardMaxDistance = FarFieldCardMaxDistance;
						MaxProjectedSize = FarFieldCardTexelDensity * MaxExtent * LumenCard.ResolutionScale;
					}
					else
					{
						CardMaxDistance = MaxDistanceFromCamera;
						MaxProjectedSize = FMath::Min(TexelDensityScale * MaxExtent * LumenCard.ResolutionScale / ViewerDistance, GLumenSceneCardMaxTexelDensity * MaxExtent);
					}

					if (GLumenSceneCardFixedDebugResolution > 0)
					{
						MaxProjectedSize = GLumenSceneCardFixedDebugResolution;
					}

					const int32 MaxSnappedRes = FMath::RoundUpToPowerOfTwo(FMath::Min(FMath::TruncToInt(MaxProjectedSize), CardMaxResolution));
					const bool bVisible = ViewerDistance < CardMaxDistance && MaxSnappedRes >= MinCardResolutionForMeshCards;
					const int32 ResLevel = FMath::FloorLog2(FMath::Max<uint32>(MaxSnappedRes, Lumen::MinCardResolution));

					if (!bVisible && LumenCard.bVisible)
					{
						CardsToHide.Add(CardIndex);
					}
					else if (bVisible && ResLevel != LumenCard.DesiredLockedResLevel)
					{
						float Distance = ViewerDistance;

						if (LumenCard.bVisible && LumenCard.DesiredLockedResLevel != ResLevel)
						{
							// Make reallocation less important than capturing new cards
							const float ResLevelDelta = FMath::Abs((int32)LumenCard.DesiredLockedResLevel - ResLevel);
							Distance += (1.0f - FMath::Clamp((ResLevelDelta + 1.0f) / 3.0f, 0.0f, 1.0f)) * 2500.0f;
						}

						FSurfaceCacheRequest Request;
						Request.ResLevel = ResLevel;
						Request.CardIndex = CardIndex;
						Request.LocalPageIndex = UINT16_MAX;
						Request.Distance = Distance;
						SurfaceCacheRequests.Add(Request);

						const int32 DistanceBin = Lumen::GetMeshCardDistanceBin(Distance);
						Histogram[DistanceBin]++;

						ensure(Request.IsLockedMip());
					}
				}
			}
		}
	}

	const TSparseSpanArray<FLumenMeshCards>& LumenMeshCards;
	const TSparseSpanArray<FLumenCard>& LumenCards;
	TArray<FVector, TInlineAllocator<2>> ViewOrigins;
	bool bOrthographicCamera;
	float LumenSceneDetail;
	int32 FirstMeshCardsIndex;
	int32 NumMeshCardsPerPacket;
	float MaxDistanceFromCamera;
	float TexelDensityScale;
	float MaxTexelDensity;

	const int32 MinCardResolution;
	const float FarFieldCardMaxDistance;
	const float FarFieldCardTexelDensity;
};

/**
 * Make sure that all mesh rendering data is prepared before we render this primitive group
 * @return Will return true it primitive group is ready to render or we need to wait until next frame
 */
bool UpdateStaticMeshes(const FLumenPrimitiveGroup& PrimitiveGroup, FLumenCardRenderer& LumenCardRenderer)
{
	bool bReadyToRender = true;

	for (FPrimitiveSceneInfo* PrimitiveSceneInfo : PrimitiveGroup.Primitives)
	{
		if (PrimitiveSceneInfo && PrimitiveSceneInfo->Proxy->AffectsDynamicIndirectLighting())
		{
			if (PrimitiveSceneInfo->Proxy->StaticMeshHasPendingStreaming())
			{
				bReadyToRender = false;
			}

			if (PrimitiveGroup.bHeightfield && PrimitiveSceneInfo->Proxy->HeightfieldHasPendingStreaming())
			{
				bReadyToRender = false;
			}
		}
	}

	return bReadyToRender;
}

bool FLumenSceneData::RecaptureCardPage(const FViewInfo& MainView, FLumenCardRenderer& LumenCardRenderer, FLumenSurfaceCacheAllocator& CaptureAtlasAllocator, FRHIGPUMask GPUMask, int32 PageTableIndex)
{
	TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;
	FLumenPageTableEntry& PageTableEntry = GetPageTableEntry(PageTableIndex);
	const FLumenCard& Card = Cards[PageTableEntry.CardIndex];
	const FLumenMeshCards& MeshCardsElement = MeshCards[Card.MeshCardsIndex];

	// Can we fit this card into the temporary card capture allocator?
	if (CaptureAtlasAllocator.IsSpaceAvailable(Card, PageTableEntry.ResLevel, /*bSinglePage*/ true))
	{
		// Allocate space in temporary allocation atlas
		FLumenSurfaceCacheAllocator::FAllocation CardCaptureAllocation;
		CaptureAtlasAllocator.Allocate(PageTableEntry, CardCaptureAllocation);
		check(CardCaptureAllocation.PhysicalPageCoord.X >= 0);

		CardPagesToRender.Add(FCardPageRenderData(
			MainView,
			Card,
			PageTableEntry.CardUVRect,
			CardCaptureAllocation.PhysicalAtlasRect,
			PageTableEntry.PhysicalAtlasRect,
			MeshCardsElement.PrimitiveGroupIndex,
			PageTableEntry.CardIndex,
			PageTableIndex,
			/*bResampleLastLighting*/ true));

		for (uint32 GPUIndex : GPUMask)
		{
			LastCapturedPageHeap[GPUIndex].Update(GetSurfaceCacheUpdateFrameIndex(), PageTableIndex);
		}
		LumenCardRenderer.NumCardTexelsToCapture += PageTableEntry.PhysicalAtlasRect.Area();
		return true;
	}

	return false;
}

/**
 * Process a throttled number of Lumen surface cache add requests
 * It will make virtual and physical allocations, and evict old pages as required
 */
void FLumenSceneData::ProcessLumenSurfaceCacheRequests(
	const FViewInfo& MainView,
	float MaxCardUpdateDistanceFromCamera,
	int32 MaxTileCapturesPerFrame,
	FLumenCardRenderer& LumenCardRenderer,
	FRHIGPUMask GPUMask,
	const TArray<FSurfaceCacheRequest, SceneRenderingAllocator>& SurfaceCacheRequests)
{
	QUICK_SCOPE_CYCLE_COUNTER(ProcessLumenSurfaceCacheRequests);

	TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;

	TArray<FVirtualPageIndex, SceneRenderingAllocator> HiResPagesToMap;
	TSparseUniqueList<int32, SceneRenderingAllocator> DirtyCards;

	FLumenSurfaceCacheAllocator CaptureAtlasAllocator;
	CaptureAtlasAllocator.Init(GetCardCaptureAtlasSizeInPages());

	for (int32 RequestIndex = 0; RequestIndex < SurfaceCacheRequests.Num(); ++RequestIndex)
	{
		const FSurfaceCacheRequest& Request = SurfaceCacheRequests[RequestIndex];

		if (Request.IsLockedMip())
		{
			// Update low-res locked (always resident) pages
			FLumenCard& Card = Cards[Request.CardIndex];

			if (Card.DesiredLockedResLevel != Request.ResLevel)
			{
				// Check if we can make this allocation at all
				bool bCanAlloc = true;

				uint8 NewLockedAllocationResLevel = Request.ResLevel;
				while (!IsPhysicalSpaceAvailable(Card, NewLockedAllocationResLevel, /*bSinglePage*/ false))
				{
					const int32 MaxFramesSinceLastUsed = 2;

					if (!EvictOldestAllocation(/*MaxFramesSinceLastUsed*/ MaxFramesSinceLastUsed, DirtyCards))
					{
						bCanAlloc = false;
						break;
					}
				}

				// Try to decrease resolution if allocation still can't be made
				while (!bCanAlloc && NewLockedAllocationResLevel > Lumen::MinResLevel)
				{
					--NewLockedAllocationResLevel;
					bCanAlloc = IsPhysicalSpaceAvailable(Card, NewLockedAllocationResLevel, /*bSinglePage*/ false);
				}

				// Can we fit this card into the temporary card capture allocator?
				if (!CaptureAtlasAllocator.IsSpaceAvailable(Card, NewLockedAllocationResLevel, /*bSinglePage*/ false))
				{
					bCanAlloc = false;
				}

				const FLumenMeshCards& MeshCardsElement = MeshCards[Card.MeshCardsIndex];
				const FLumenPrimitiveGroup& PrimitiveGroup = PrimitiveGroups[MeshCardsElement.PrimitiveGroupIndex];

				if (bCanAlloc && UpdateStaticMeshes(PrimitiveGroup, LumenCardRenderer))
				{
					Card.bVisible = true;
					Card.DesiredLockedResLevel = Request.ResLevel;

					const bool bResampleLastLighting = Card.IsAllocated();
					const bool bCopyFromSelf = bAllowCardDownsampleFromSelf && Card.IsAllocated() && NewLockedAllocationResLevel <= Card.MinAllocatedResLevel;

					// Free previous MinAllocatedResLevel
					FreeVirtualSurface(Card, Card.MinAllocatedResLevel, Card.MinAllocatedResLevel);

					// Free anything lower res than the new res level
					FreeVirtualSurface(Card, Card.MinAllocatedResLevel, NewLockedAllocationResLevel - 1);


					const bool bLockPages = true;
					ReallocVirtualSurface(Card, Request.CardIndex, NewLockedAllocationResLevel, bLockPages);

					int32 CopyCardIndex = INDEX_NONE;
					bool bAxisXFlipped = false;
					if (bCopyFromSelf)
					{
						CopyCardIndex = Request.CardIndex;
					}
					else if (const FLumenCardSharingInfo* CardSharingInfo = FindMatchingCardForCopy(Card.CardSharingId, Card.MinAllocatedResLevel))
					{
						check(Card.MinAllocatedResLevel <= CardSharingInfo->MinAllocatedResLevel);
						CopyCardIndex = CardSharingInfo->CardIndex;
						bAxisXFlipped = Card.bAxisXFlipped != CardSharingInfo->bAxisXFlipped;
					}

					// Map and update all pages
					FLumenSurfaceMipMap& MipMap = Card.GetMipMap(Card.MinAllocatedResLevel);
					for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
					{
						const int32 PageIndex = MipMap.GetPageTableIndex(LocalPageIndex);
						FLumenPageTableEntry& PageTableEntry = GetPageTableEntry(PageIndex);

						if (!PageTableEntry.IsMapped())
						{
							MapSurfaceCachePage(MipMap, PageIndex, GPUMask);
							check(PageTableEntry.IsMapped());

							// Allocate space in temporary allocation atlas
							FLumenSurfaceCacheAllocator::FAllocation CardCaptureAllocation;
							CaptureAtlasAllocator.Allocate(PageTableEntry, CardCaptureAllocation);
							check(CardCaptureAllocation.PhysicalPageCoord.X >= 0);

							CardPagesToRender.Add(FCardPageRenderData(
								MainView,
								Card,
								PageTableEntry.CardUVRect,
								CardCaptureAllocation.PhysicalAtlasRect,
								PageTableEntry.PhysicalAtlasRect,
								MeshCardsElement.PrimitiveGroupIndex,
								Request.CardIndex,
								PageIndex,
								bResampleLastLighting,
								bAxisXFlipped,
								CopyCardIndex));

							if (CopyCardIndex >= 0)
							{
								LumenCardRenderer.bHasAnyCardCopy = true;
							}

							for (uint32 GPUIndex : GPUMask)
							{
								LastCapturedPageHeap[GPUIndex].Update(GetSurfaceCacheUpdateFrameIndex(), PageIndex);
							}
							LumenCardRenderer.NumCardTexelsToCapture += PageTableEntry.PhysicalAtlasRect.Area();
						}
					}

					DirtyCards.Add(Request.CardIndex);
				}
			}
		}
		else
		{
			// Hi-Res
			if (Cards.IsAllocated(Request.CardIndex))
			{
				FLumenCard& Card = Cards[Request.CardIndex];

				if (Card.bVisible && Card.MinAllocatedResLevel >= 0 && Request.ResLevel > Card.MinAllocatedResLevel)
				{
					HiResPagesToMap.Add(FVirtualPageIndex(Request.CardIndex, Request.ResLevel, Request.LocalPageIndex));
				}
			}
		}

		if (CardPagesToRender.Num() + HiResPagesToMap.Num() >= MaxTileCapturesPerFrame)
		{
			break;
		}
	}

	// Process hi-res optional pages after locked low res ones are done
	for (const FVirtualPageIndex& VirtualPageIndex : HiResPagesToMap)
	{
		FLumenCard& Card = Cards[VirtualPageIndex.CardIndex];

		if (VirtualPageIndex.ResLevel > Card.MinAllocatedResLevel)
		{
			// Make room for new physical allocations
			bool bCanAlloc = true;
			while (!IsPhysicalSpaceAvailable(Card, VirtualPageIndex.ResLevel, /*bSinglePage*/ true))
			{
				// Don't want to evict pages which may be picked up a jittering tile feedback
				const int32 MaxFramesSinceLastUsed = Lumen::GetFeedbackBufferTileSize() * Lumen::GetFeedbackBufferTileSize();

				if (!EvictOldestAllocation(MaxFramesSinceLastUsed, DirtyCards))
				{
					bCanAlloc = false;
					break;
				}
			}

			// Can we fit this card into the temporary card capture allocator?
			if (!CaptureAtlasAllocator.IsSpaceAvailable(Card, VirtualPageIndex.ResLevel, /*bSinglePage*/ true))
			{
				bCanAlloc = false;
			}

			const FLumenMeshCards& MeshCardsElement = MeshCards[Card.MeshCardsIndex];
			const FLumenPrimitiveGroup& PrimitiveGroup = PrimitiveGroups[MeshCardsElement.PrimitiveGroupIndex];

			if (bCanAlloc && UpdateStaticMeshes(PrimitiveGroup, LumenCardRenderer))
			{
				const bool bLockPages = false;
				const bool bResampleLastLighting = Card.IsAllocated();

				ReallocVirtualSurface(Card, VirtualPageIndex.CardIndex, VirtualPageIndex.ResLevel, bLockPages);

				FLumenSurfaceMipMap& MipMap = Card.GetMipMap(VirtualPageIndex.ResLevel);
				const int32 PageIndex = MipMap.GetPageTableIndex(VirtualPageIndex.LocalPageIndex);
				FLumenPageTableEntry& PageTableEntry = GetPageTableEntry(PageIndex);

				if (!PageTableEntry.IsMapped())
				{
					MapSurfaceCachePage(MipMap, PageIndex, GPUMask);
					check(PageTableEntry.IsMapped());

					// Allocate space in temporary allocation atlas
					FLumenSurfaceCacheAllocator::FAllocation CardCaptureAllocation;
					CaptureAtlasAllocator.Allocate(PageTableEntry, CardCaptureAllocation);
					check(CardCaptureAllocation.PhysicalPageCoord.X >= 0);

					CardPagesToRender.Add(FCardPageRenderData(
						MainView,
						Card,
						PageTableEntry.CardUVRect,
						CardCaptureAllocation.PhysicalAtlasRect,
						PageTableEntry.PhysicalAtlasRect,
						MeshCardsElement.PrimitiveGroupIndex,
						VirtualPageIndex.CardIndex,
						PageIndex,
						bResampleLastLighting));

					for (uint32 GPUIndex : GPUMask)
					{
						LastCapturedPageHeap[GPUIndex].Update(GetSurfaceCacheUpdateFrameIndex(), PageIndex);
					}
					LumenCardRenderer.NumCardTexelsToCapture += PageTableEntry.PhysicalAtlasRect.Area();
					DirtyCards.Add(VirtualPageIndex.CardIndex);
				}
			}
		}
	}

	// Reconcile removal, addition, and res change of cards. This must be done after querying CardSharingInfoMap because we
	// copy cards from the physical atlas before current frame capture data is merged back so CardSharingInfoMap needs to
	// reflect the state right after last frame captures
	FlushPendingCardSharingInfos();

	// Process any surface cache page invalidation requests
	{
		QUICK_SCOPE_CYCLE_COUNTER(SceneCardCaptureInvalidation);

		if (CVarLumenSceneCardCaptureEnableInvalidation.GetValueOnRenderThread() == 0)
		{
			for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; GPUIndex++)
			{
				PagesToRecaptureHeap[GPUIndex].Clear();
			}
		}

		FBinaryHeap<uint32, uint32>& PageHeap = PagesToRecaptureHeap[GPUMask.GetFirstIndex()];
		while (PageHeap.Num() > 0)
		{
			const uint32 PageTableIndex = PageHeap.Top();
			if (RecaptureCardPage(MainView, LumenCardRenderer, CaptureAtlasAllocator, GPUMask, PageTableIndex))
			{
				PageHeap.Pop();
			}
			else
			{
				break;
			}
		}
	}

	// Finally process card refresh to capture any material updates, or render cards that need to be initialized for the first time on
	// a given GPU in multi-GPU scenarios.  Uninitialized cards on a particular GPU will have a zero captured frame index set when the
	// card was allocated.  A zero frame index otherwise can't occur on a card, because the constructor sets SurfaceCacheUpdateFrameIndex
	// to 1, and IncrementSurfaceCacheUpdateFrameIndex skips over zero if it happens to wrap around.
	{
		QUICK_SCOPE_CYCLE_COUNTER(SceneCardCaptureRefresh);

		int32 NumTexelsLeftToRefresh = GetCardCaptureRefreshNumTexels();
		int32 NumPagesLeftToRefesh = FMath::Min<int32>((int32)GetCardCaptureRefreshNumPages(), MaxTileCapturesPerFrame - CardPagesToRender.Num());

		FBinaryHeap<uint32,uint32>& PageHeap = LastCapturedPageHeap[GPUMask.GetFirstIndex()];

		bool bCanCapture = true;
		while (PageHeap.Num() > 0 && bCanCapture)
		{
			bCanCapture = false;

			const uint32 PageTableIndex = PageHeap.Top();
			const uint32 CapturedSurfaceCacheFrameIndex = PageHeap.GetKey(PageTableIndex);

			const int32 FramesSinceLastUpdated = GetSurfaceCacheUpdateFrameIndex() - CapturedSurfaceCacheFrameIndex;
			if (FramesSinceLastUpdated > 0)
			{
#if WITH_MGPU
				// Limit number of re-captured texels and pages per frame, except always allow captures of uninitialized
				// cards where the captured frame index is zero (don't count them against the throttled limits).
				// Uninitialized cards on a particular GPU will always be at the front of the heap, due to the zero index,
				// so even if the limits are set to zero, we'll still process them if needed (the limit comparisons below
				// are >= 0, and will pass if nothing has been decremented from the limits yet).
				if ((CapturedSurfaceCacheFrameIndex != 0) || (GNumExplicitGPUsForRendering == 1))
#endif
				{
					FLumenPageTableEntry& PageTableEntry = GetPageTableEntry(PageTableIndex);
					check(PageTableEntry.IsMapped());

					if (PageTableEntry.IsMapped())
					{
						const FLumenCard& Card = Cards[PageTableEntry.CardIndex];
						FLumenMipMapDesc MipMapDesc;
						Card.GetMipMapDesc(PageTableEntry.ResLevel, MipMapDesc);
						NumTexelsLeftToRefresh -= MipMapDesc.PageResolution.X * MipMapDesc.PageResolution.Y;
						NumPagesLeftToRefesh -= 1;
					}
				}

				if (NumTexelsLeftToRefresh >= 0 && NumPagesLeftToRefesh >= 0)
				{
					bCanCapture = RecaptureCardPage(MainView, LumenCardRenderer, CaptureAtlasAllocator, GPUMask, PageTableIndex);
				}
			}
		}
	}

	// Evict pages which weren't used recently
	if (!Lumen::IsSurfaceCacheFrozen())
	{
		uint32 MaxFramesSinceLastUsed = FMath::Max(GSurfaceCacheNumFramesToKeepUnusedPages, 0);
		while (EvictOldestAllocation(MaxFramesSinceLastUsed, DirtyCards))
		{
		}
	}

	for (int32 CardIndex : DirtyCards.Array)
	{
		FLumenCard& Card = Cards[CardIndex];
		UpdateCardMipMapHierarchy(Card);
		CardIndicesToUpdateInBuffer.Add(CardIndex);
	}
}

void ProcessSceneRemoveOpsReadbackData(FLumenSceneData& LumenSceneData, const FLumenSceneReadback::FRemoveOp* RemoveOpsData)
{
	if (RemoveOpsData)
	{
		// #lumen_todo: Temporary workaround until we optimized FLumenSurfaceCacheAllocator::Free to use fast batched removes
		int32 NumMeshCardsRemoves = 0;
		const int32 MaxMeshCardsRemoves = LumenScene::GetMaxMeshCardsRemovesPerFrame();

		// First element encodes array size
		const int32 HeaderSize = 1;
		const int32 NumReadbackElements = FMath::Min<int32>(RemoveOpsData[0].PrimitiveGroupIndex, LumenSceneData.SceneReadback.GetMaxRemoveOps() - HeaderSize);

		for (int32 ElementIndex = 0; ElementIndex < NumReadbackElements; ++ElementIndex)
		{
			if (NumMeshCardsRemoves >= MaxMeshCardsRemoves)
			{
				break;
			}

			const uint32 PrimitiveGroupIndex = RemoveOpsData[ElementIndex + HeaderSize].PrimitiveGroupIndex;

			if (LumenSceneData.PrimitiveGroups.IsAllocated(PrimitiveGroupIndex))
			{
				const FLumenPrimitiveGroup& PrimitiveGroup = LumenSceneData.PrimitiveGroups[PrimitiveGroupIndex];
				if (PrimitiveGroup.bValidMeshCards && PrimitiveGroup.MeshCardsIndex >= 0)
				{
					LumenSceneData.RemoveMeshCards(PrimitiveGroupIndex);
					++NumMeshCardsRemoves;
				}
			}
		}
	}
}

void ProcessSceneAddOpsReadbackData(FLumenSceneData& LumenSceneData, const FLumenSceneReadback::FAddOp* AddOpsData)
{
	if (AddOpsData)
	{
		TArray<FMeshCardsAdd, SceneRenderingAllocator> MeshCardsAdds;

		// First element encodes array size
		const int32 HeaderSize = 1;
		const int32 NumReadbackElements = FMath::Min<int32>(AddOpsData[0].PrimitiveGroupIndex, LumenSceneData.SceneReadback.GetMaxAddOps() - HeaderSize);

		for (int32 ElementIndex = 0; ElementIndex < NumReadbackElements; ++ElementIndex)
		{
			const FLumenSceneReadback::FAddOp AddOp = AddOpsData[ElementIndex + HeaderSize];

			if (LumenSceneData.PrimitiveGroups.IsAllocated(AddOp.PrimitiveGroupIndex))
			{
				const FLumenPrimitiveGroup& PrimitiveGroup = LumenSceneData.PrimitiveGroups[AddOp.PrimitiveGroupIndex];
				if (PrimitiveGroup.bValidMeshCards && PrimitiveGroup.MeshCardsIndex == -1)
				{
					FMeshCardsAdd& MeshCardsAdd = MeshCardsAdds.AddDefaulted_GetRef();
					MeshCardsAdd.PrimitiveGroupIndex = AddOp.PrimitiveGroupIndex;
					MeshCardsAdd.DistanceSquared = AddOp.DistanceSq;
				}
			}
		}

		if (MeshCardsAdds.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(SortAdds);

			struct FSortBySmallerDistance
			{
				FORCEINLINE bool operator()(const FMeshCardsAdd& A, const FMeshCardsAdd& B) const
				{
					return A.DistanceSquared < B.DistanceSquared;
				}
			};

			MeshCardsAdds.Sort(FSortBySmallerDistance());
		}

		const int32 MeshCardsToAddPerFrame = LumenScene::GetMaxMeshCardsToAddPerFrame();

		for (int32 MeshCardsIndex = 0; MeshCardsIndex < FMath::Min(MeshCardsAdds.Num(), MeshCardsToAddPerFrame); ++MeshCardsIndex)
		{
			const FMeshCardsAdd& MeshCardsAdd = MeshCardsAdds[MeshCardsIndex];
			LumenSceneData.AddMeshCards(MeshCardsAdd.PrimitiveGroupIndex);
		}
	}
}

void UpdateSurfaceCachePrimitives(
	FLumenSceneData& LumenSceneData,
	const TArray<FVector, TInlineAllocator<2>>& LumenSceneCameraOrigins,
	bool bOrthographicCamera,
	float LumenSceneDetail,
	float MaxCardUpdateDistanceFromCamera,
	FLumenCardRenderer& LumenCardRenderer,
	bool bAddTranslucentToCache)
{
	QUICK_SCOPE_CYCLE_COUNTER(UpdateSurfaceCachePrimitives);

	{
		const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance() && GLumenSceneParallelUpdate != 0;
		const int32 NumPrimitiveTasks = ParallelForImpl::GetNumberOfThreadTasks(LumenSceneData.PrimitiveCullingInfos.GetMaxIndex(), GLumenScenePrimitivesPerTask, EParallelForFlags::None);
		const int32 NumPrimitivesPerTask = FMath::DivideAndRoundUp(LumenSceneData.PrimitiveCullingInfos.GetMaxIndex(), NumPrimitiveTasks);

		TArray<FLumenSurfaceCacheCullPrimitivesTask, SceneRenderingAllocator> PrimitiveTasks;
		PrimitiveTasks.Reserve(NumPrimitiveTasks);

		for (int32 TaskIndex = 0; TaskIndex < NumPrimitiveTasks; ++TaskIndex)
		{
			PrimitiveTasks.Emplace(
				LumenSceneData.PrimitiveCullingInfos,
				LumenSceneCameraOrigins,
				bOrthographicCamera,
				LumenSceneDetail,
				MaxCardUpdateDistanceFromCamera,
				TaskIndex * NumPrimitivesPerTask,
				NumPrimitivesPerTask,
				bAddTranslucentToCache);
		}

		ParallelFor(
			NumPrimitiveTasks,
			[&PrimitiveTasks](int32 Index)
			{
				PrimitiveTasks[Index].AnyThreadTask();
			},
			!bExecuteInParallel);

		TArray<FInstanceRange, SceneRenderingAllocator> InstanceRanges;
		TArray<int32, SceneRenderingAllocator> RebasedRangeOffsets;
		int32 NumInstances = 0;

		for (int32 TaskIndex = 0; TaskIndex < NumPrimitiveTasks; ++TaskIndex)
		{
			const FLumenSurfaceCacheCullPrimitivesTask& Task = PrimitiveTasks[TaskIndex];

			InstanceRanges.Reserve(InstanceRanges.Num() + Task.InstanceCullingRanges.Num());
			RebasedRangeOffsets.Reserve(RebasedRangeOffsets.Num() + Task.InstanceCullingRanges.Num());

			for (const FInstanceRange& InstanceRange : Task.InstanceCullingRanges)
			{
				InstanceRanges.Add(InstanceRange);
				RebasedRangeOffsets.Add(NumInstances);
				NumInstances += InstanceRange.Num;
			}
		}
		RebasedRangeOffsets.Add(NumInstances);

		const int32 NumInstanceTasks = ParallelForImpl::GetNumberOfThreadTasks(NumInstances, GLumenScenePrimitivesPerTask, EParallelForFlags::None);
		const int32 NumInstancesPerTask = FMath::DivideAndRoundUp(NumInstances, NumInstanceTasks);
		
		TArray<FLumenSurfaceCacheCullInstancesTask, SceneRenderingAllocator> InstanceTasks;
		InstanceTasks.Reserve(NumInstanceTasks);

		for (int32 TaskIndex = 0; TaskIndex < NumInstanceTasks; ++TaskIndex)
		{
			InstanceTasks.Emplace(
				LumenSceneData.InstanceCullingInfos,
				InstanceRanges,
				RebasedRangeOffsets,
				LumenSceneCameraOrigins,
				bOrthographicCamera,
				LumenSceneDetail,
				MaxCardUpdateDistanceFromCamera,
				TaskIndex * NumInstancesPerTask,
				NumInstancesPerTask,
				bAddTranslucentToCache);
		}

		ParallelFor(
			NumInstanceTasks,
			[&InstanceTasks](int32 Index)
			{
				InstanceTasks[Index].AnyThreadTask();
			},
			!bExecuteInParallel);

		TArray<FMeshCardsAdd, SceneRenderingAllocator> MeshCardsAdds;

		for (int32 TaskIndex = 0; TaskIndex < NumPrimitiveTasks; ++TaskIndex)
		{
			const FLumenSurfaceCacheCullPrimitivesTask& Task = PrimitiveTasks[TaskIndex];
			LumenSceneData.NumMeshCardsToAdd += Task.MeshCardsAdds.Num();

			// Append requests to the global array
			{
				MeshCardsAdds.Reserve(MeshCardsAdds.Num() + Task.MeshCardsAdds.Num());

				for (int32 RequestIndex = 0; RequestIndex < Task.MeshCardsAdds.Num(); ++RequestIndex)
				{
					MeshCardsAdds.Add(Task.MeshCardsAdds[RequestIndex]);
				}
			}

			// #lumen_todo: Temporary workaround until we optimized FLumenSurfaceCacheAllocator::Free to use fast batched removes
			int32 NumMeshCardsRemoves = 0;
			const int32 MaxMeshCardsRemoves = LumenScene::GetMaxMeshCardsRemovesPerFrame();

			for (const FMeshCardsRemove& MeshCardsRemove : Task.MeshCardsRemoves)
			{
				if (NumMeshCardsRemoves >= MaxMeshCardsRemoves)
				{
					break;
				}
				++NumMeshCardsRemoves;

				LumenSceneData.RemoveMeshCards(MeshCardsRemove.PrimitiveGroupIndex);
			}
		}

		for (int32 TaskIndex = 0; TaskIndex < NumInstanceTasks; ++TaskIndex)
		{
			const FLumenSurfaceCacheCullInstancesTask& Task = InstanceTasks[TaskIndex];
			LumenSceneData.NumMeshCardsToAdd += Task.MeshCardsAdds.Num();

			// Append requests to the global array
			{
				MeshCardsAdds.Reserve(MeshCardsAdds.Num() + Task.MeshCardsAdds.Num());

				for (int32 RequestIndex = 0; RequestIndex < Task.MeshCardsAdds.Num(); ++RequestIndex)
				{
					MeshCardsAdds.Add(Task.MeshCardsAdds[RequestIndex]);
				}
			}

			// #lumen_todo: Temporary workaround until we optimized FLumenSurfaceCacheAllocator::Free to use fast batched removes
			int32 NumMeshCardsRemoves = 0;
			const int32 MaxMeshCardsRemoves = LumenScene::GetMaxMeshCardsRemovesPerFrame();

			for (const FMeshCardsRemove& MeshCardsRemove : Task.MeshCardsRemoves)
			{
				if (NumMeshCardsRemoves >= MaxMeshCardsRemoves)
				{
					break;
				}
				++NumMeshCardsRemoves;

				LumenSceneData.RemoveMeshCards(MeshCardsRemove.PrimitiveGroupIndex);
			}
		}

		if (MeshCardsAdds.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(SortAdds);

			struct FSortBySmallerDistance
			{
				FORCEINLINE bool operator()(const FMeshCardsAdd& A, const FMeshCardsAdd& B) const
				{
					return A.DistanceSquared < B.DistanceSquared;
				}
			};

			MeshCardsAdds.Sort(FSortBySmallerDistance());
		}

		const int32 MeshCardsToAddPerFrame = LumenScene::GetMaxMeshCardsToAddPerFrame();

		for (int32 MeshCardsIndex = 0; MeshCardsIndex < FMath::Min(MeshCardsAdds.Num(), MeshCardsToAddPerFrame); ++MeshCardsIndex)
		{
			const FMeshCardsAdd& MeshCardsAdd = MeshCardsAdds[MeshCardsIndex];
			LumenSceneData.AddMeshCards(MeshCardsAdd.PrimitiveGroupIndex);
		}
	}
}

void UpdateSurfaceCacheMeshCards(
	FLumenSceneData& LumenSceneData,
	FLumenSceneData::FFeedbackData LumenFeedbackData,
	const TArray<FVector, TInlineAllocator<2>>& LumenSceneCameraOrigins,
	bool bOrthographicCamera,
	float LumenSceneDetail,
	float MaxCardUpdateDistanceFromCamera,
	TArray<FSurfaceCacheRequest, SceneRenderingAllocator>& SurfaceCacheRequests,
	const FViewFamilyInfo& ViewFamily)
{
	QUICK_SCOPE_CYCLE_COUNTER(UpdateMeshCards);

	const int32 NumMeshCardsPerTask = FMath::Max(GLumenSceneMeshCardsPerTask, 1);
	const int32 NumTasks = FMath::DivideAndRoundUp(LumenSceneData.MeshCards.Num(), NumMeshCardsPerTask);
	if (NumTasks == 0)
	{
		return;
	}

	int32 RequestHistogram[Lumen::NumDistanceBuckets] { 0 };

	TArray<FLumenSurfaceCacheUpdateMeshCardsTask, SceneRenderingAllocator> Tasks;
	Tasks.Reserve(NumTasks);

	for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
	{
		Tasks.Emplace(
			LumenSceneData.MeshCards,
			LumenSceneData.Cards,
			LumenSceneCameraOrigins,
			bOrthographicCamera,
			LumenSceneData.SurfaceCacheResolution,
			LumenSceneDetail,
			MaxCardUpdateDistanceFromCamera,
			TaskIndex * NumMeshCardsPerTask,
			NumMeshCardsPerTask);
	}

	const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance() && GLumenSceneParallelUpdate != 0;

	ParallelFor(Tasks.Num(),
		[&Tasks](int32 Index)
		{
			Tasks[Index].AnyThreadTask();
		},
		!bExecuteInParallel);

	uint32 TotalSurfaceCacheRequests = 0;
	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FLumenSurfaceCacheUpdateMeshCardsTask& Task = Tasks[TaskIndex];
		TotalSurfaceCacheRequests += Task.SurfaceCacheRequests.Num();
	}

	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FLumenSurfaceCacheUpdateMeshCardsTask& Task = Tasks[TaskIndex];
		LumenSceneData.NumLockedCardsToUpdate += Task.SurfaceCacheRequests.Num();

		for (int32 i = 0; i < Lumen::NumDistanceBuckets; ++i)
		{
			RequestHistogram[i] += Task.Histogram[i];
		}

		for (int32 CardIndex : Task.CardsToHide)
		{
			FLumenCard& Card = LumenSceneData.Cards[CardIndex];

			if (Card.bVisible)
			{
				LumenSceneData.RemoveCardFromAtlas(CardIndex);
				Card.bVisible = false;
			}
		}
	}

	LumenSceneData.UpdateSurfaceCacheFeedback(LumenFeedbackData, LumenSceneCameraOrigins, Tasks[0].SurfaceCacheRequests, ViewFamily, RequestHistogram);

	int32 SurfaceCacheRequestsCount = 0;
	int32 LastBucketRequestCount = 0;
	int32 LastBucketIndex = 0;
	for (; LastBucketIndex < Lumen::NumDistanceBuckets; ++LastBucketIndex)
	{
		SurfaceCacheRequestsCount += RequestHistogram[LastBucketIndex];

		if (SurfaceCacheRequestsCount >= GetMaxLumenSceneCardCapturesPerFrame())
		{
			LastBucketRequestCount = GLumenSceneCardCapturesPerFrame - (SurfaceCacheRequestsCount - RequestHistogram[LastBucketIndex]);
			SurfaceCacheRequestsCount = GLumenSceneCardCapturesPerFrame;
			break;
		}
	}

	if (SurfaceCacheRequestsCount == 0)
	{
		return;
	}

	SurfaceCacheRequests.Reserve(SurfaceCacheRequestsCount);
	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FLumenSurfaceCacheUpdateMeshCardsTask& Task = Tasks[TaskIndex];

		for (int32 RequestIndex = 0; RequestIndex < Task.SurfaceCacheRequests.Num(); ++RequestIndex)
		{
			const FSurfaceCacheRequest& Request = Task.SurfaceCacheRequests[RequestIndex];
			const int32 BucketIndex = Lumen::GetMeshCardDistanceBin(Request.Distance);
			if (BucketIndex > LastBucketIndex)
			{
				continue;
			}

			if (BucketIndex == LastBucketIndex)
			{
				if (LastBucketRequestCount == 0)
				{
					continue;
				}

				--LastBucketRequestCount;
			}

			SurfaceCacheRequests.Add(Request);
			if (--SurfaceCacheRequestsCount == 0)
			{
				break;
			}
		}

		if (SurfaceCacheRequestsCount == 0)
		{
			break;
		}
	}
}

extern void UpdateLumenScenePrimitives(FRHIGPUMask GPUMask, FScene* Scene);

void AllocateResampledCardCaptureAtlas(FRDGBuilder& GraphBuilder, FIntPoint CardCaptureAtlasSize, FResampledCardCaptureAtlas& CardCaptureAtlas)
{
	CardCaptureAtlas.Size = CardCaptureAtlasSize;

	CardCaptureAtlas.DirectLighting = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			Lumen::GetDirectLightingAtlasFormat(),
			FClearValueBinding::Green,
			TexCreate_ShaderResource | TexCreate_NoFastClear | TexCreate_UAV),
		TEXT("Lumen.ResampledCardCaptureDirectLighting"));

	CardCaptureAtlas.IndirectLighting = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			Lumen::GetIndirectLightingAtlasFormat(),
			FClearValueBinding::Green,
			TexCreate_ShaderResource | TexCreate_NoFastClear | TexCreate_UAV),
		TEXT("Lumen.ResampledCardCaptureIndirectLighting"));

	CardCaptureAtlas.NumFramesAccumulated = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			Lumen::GetNumFramesAccumulatedAtlasFormat(),
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_NoFastClear | TexCreate_UAV),
		TEXT("Lumen.ResampledCardCaptureNumFramesAccumulated"));

	const FIntPoint CardCaptureAtlasSizeInTiles = CardCaptureAtlasSize / Lumen::CardTileSize;
	CardCaptureAtlas.TileShadowDownsampleFactor = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateBufferDesc(
			sizeof(uint32),
			CardCaptureAtlasSizeInTiles.X * CardCaptureAtlasSizeInTiles.Y * Lumen::CardTileShadowDownsampleFactorDwords),
		TEXT("Lumen.ResampledCardCaptureTileShadowDownsampleFactorAtlas"));
}

class FResampleLightingHistoryToCardCaptureAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FResampleLightingHistoryToCardCaptureAtlasCS);
	SHADER_USE_PARAMETER_STRUCT(FResampleLightingHistoryToCardCaptureAtlasCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DirectLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectLightingAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadiosityNumFramesAccumulatedAtlas)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, TileShadowDownsampleFactorAtlasForResampling)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWDirectLightingCardCaptureAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWRadiosityCardCaptureAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<UNORM float>, RWRadiosityNumFramesAccumulatedCardCaptureAtlas)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, RWTileShadowDownsampleFactorAtlas)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, NewCardPageResampleData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NewCardTileResampleData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, RectCoordBuffer)
		SHADER_PARAMETER(uint32, CardCaptureAtlasWidthInTiles)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FResampleLightingHistoryToCardCaptureAtlasCS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "ResampleLightingHistoryToCardCaptureAtlasCS", SF_Compute);

class FLumenCardResamplePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardResamplePS);
	SHADER_USE_PARAMETER_STRUCT(FLumenCardResamplePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceAlbedoAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceOpacityAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceNormalAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceEmissiveAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceDepthAtlas)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, SourceCardData)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenCardResamplePS, "/Engine/Private/Lumen/SurfaceCache/LumenSurfaceCache.usf", "LumenCardResamplePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardResampleParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardResamplePS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void ResampleLumenCards(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FScene* Scene,
	const FLumenSceneData& LumenSceneData,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	TConstArrayView<FCardPageRenderData> CardPagesToRender,
	bool bHasAnyCardCopy,
	FCardCaptureAtlas& OutCardCaptureAtlas,
	FResampledCardCaptureAtlas& OutCardCaptureLightingAtlas,
	FRDGBufferSRVRef& OutCardCaptureRectBufferSRV)
{
	if (!FrameTemporaries.PageTableBufferSRV || !FrameTemporaries.CardBufferSRV)
	{
		return;
	}

	const bool bNeedsResample = bHasAnyCardCopy || GLumenSceneSurfaceCacheResampleLighting != 0;
	const uint32 NumRects = CardPagesToRender.Num();

	FRDGUploadData<FUintVector4> CardCaptureRectArray(GraphBuilder, NumRects);
	FRDGUploadData<FUintVector4> CardPageResampleDataArray(GraphBuilder, bNeedsResample ? NumRects * 2 : 1);
	uint32 NumCaptureTiles = 0;

	for (uint32 Index = 0; Index < NumRects; ++Index)
	{
		const FCardPageRenderData& CardPageRenderData = CardPagesToRender[Index];

		const FIntPoint RectSizeInTiles = CardPageRenderData.CardCaptureAtlasRect.Size() / Lumen::CardTileSize;
		NumCaptureTiles += RectSizeInTiles.X * RectSizeInTiles.Y;

		FUintVector4& Rect = CardCaptureRectArray[Index];
		Rect.X = CardPageRenderData.CardCaptureAtlasRect.Min.X;
		Rect.Y = CardPageRenderData.CardCaptureAtlasRect.Min.Y;
		Rect.Z = CardPageRenderData.CardCaptureAtlasRect.Max.X;
		Rect.W = CardPageRenderData.CardCaptureAtlasRect.Max.Y;

		if (bNeedsResample)
		{
			FUintVector4& CardPageResampleData0 = CardPageResampleDataArray[Index * 2 + 0];
			FUintVector4& CardPageResampleData1 = CardPageResampleDataArray[Index * 2 + 1];
			CardPageResampleData0.X = CardPageRenderData.bResampleLastLighting ? CardPageRenderData.CardIndex : -1;
			CardPageResampleData0.Y = CardPageRenderData.NeedsRender() ? 0 : CardPageRenderData.CopyCardIndex + 1;
			CardPageResampleData0.Y |= CardPageRenderData.bAxisXFlipped ? 0x80000000 : 0;
			CardPageResampleData1 = FUintVector4(
				*(const uint32*)&CardPageRenderData.CardUVRect.X,
				*(const uint32*)&CardPageRenderData.CardUVRect.Y,
				*(const uint32*)&CardPageRenderData.CardUVRect.Z,
				*(const uint32*)&CardPageRenderData.CardUVRect.W);
		}
	}

	FRDGBufferRef CardCaptureRectBuffer = CreateUploadBuffer(GraphBuilder, TEXT("Lumen.CardCaptureRects"),
		sizeof(FUintVector4), FMath::RoundUpToPowerOfTwo(NumRects),
		CardCaptureRectArray);
	OutCardCaptureRectBufferSRV = GraphBuilder.CreateSRV(CardCaptureRectBuffer, PF_R32G32B32A32_UINT);
	
	if (!bNeedsResample)
	{
		return;
	}

	FRDGBufferRef CardPageResampleDataBuffer = CreateUploadBuffer(GraphBuilder, TEXT("Lumen.CardPageResampleDataBuffer"),
		sizeof(FUintVector4), FMath::RoundUpToPowerOfTwo(NumRects * 2),
		CardPageResampleDataArray);
	FRDGBufferSRVRef CardPageResampleDataBufferSRV = GraphBuilder.CreateSRV(CardPageResampleDataBuffer, PF_R32G32B32A32_UINT);

	// Resample card material attributes if requested. Otherwise, clear
	if (bHasAnyCardCopy)
	{
		LumenScene::AllocateCardCaptureAtlas(GraphBuilder, LumenSceneData.GetCardCaptureAtlasSize(), OutCardCaptureAtlas, Scene->GetShaderPlatform());

		auto* PassParameters = GraphBuilder.AllocParameters<FLumenCardResampleParameters>();
		PassParameters->PS.View = View.ViewUniformBuffer;
		PassParameters->PS.LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
		PassParameters->PS.SourceAlbedoAtlas = FrameTemporaries.AlbedoAtlas;
		PassParameters->PS.SourceOpacityAtlas = FrameTemporaries.OpacityAtlas;
		PassParameters->PS.SourceNormalAtlas = FrameTemporaries.NormalAtlas;
		PassParameters->PS.SourceEmissiveAtlas = FrameTemporaries.EmissiveAtlas;
		PassParameters->PS.SourceDepthAtlas = FrameTemporaries.DepthAtlas;
		PassParameters->PS.SourceCardData = CardPageResampleDataBufferSRV;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutCardCaptureAtlas.Albedo, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(OutCardCaptureAtlas.Normal, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(OutCardCaptureAtlas.Emissive, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(OutCardCaptureAtlas.DepthStencil, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

		auto PixelShader = View.ShaderMap->GetShader<FLumenCardResamplePS>();

		FPixelShaderUtils::AddRasterizeToRectsPass<FLumenCardResamplePS>(
			GraphBuilder,
			View.ShaderMap,
			RDG_EVENT_NAME("ResampleLumenCards"),
			PixelShader,
			PassParameters,
			OutCardCaptureAtlas.Size,
			OutCardCaptureRectBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_Always,
			true, CF_Always, SO_Replace, SO_Replace, SO_Replace,
			false, CF_Always, SO_Replace, SO_Replace, SO_Replace,
			0xff, 0xff>::GetRHI());
	}

	// Try to resample direct lighting and indirect lighting (radiosity) from existing surface cache to new captured cards
	if (GLumenSceneSurfaceCacheResampleLighting != 0)
	{
		AllocateResampledCardCaptureAtlas(GraphBuilder, LumenSceneData.GetCardCaptureAtlasSize(), OutCardCaptureLightingAtlas);

		FRDGUploadData<uint32> CardTileResampleDataArray(GraphBuilder, NumCaptureTiles);

		for (int32 RectIndex = 0, TileIndex = 0; RectIndex < CardPagesToRender.Num(); RectIndex++)
		{
			const FCardPageRenderData& CardPageRenderData = CardPagesToRender[RectIndex];
			const FIntPoint RectSizeInTiles = CardPageRenderData.CardCaptureAtlasRect.Size() / Lumen::CardTileSize;

			for (uint32 TileY = 0; TileY < (uint32)RectSizeInTiles.Y; ++TileY)
			{
				for (uint32 TileX = 0; TileX < (uint32)RectSizeInTiles.X; ++TileX)
				{
					const uint32 PackedTileData = (RectIndex << 8u) | (TileY << 4u) | TileX;
					CardTileResampleDataArray[TileIndex++] = PackedTileData;
				}
			}
		}

		FRDGBufferRef CardTileResampleDataBuffer = CreateUploadBuffer(GraphBuilder, TEXT("Lumen.CardTileResampleDataBuffer"),
			sizeof(uint32), FMath::RoundUpToPowerOfTwo(NumCaptureTiles),
			CardTileResampleDataArray);
		FRDGBufferSRVRef CardTileResampleDataBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardTileResampleDataBuffer, PF_R32_UINT));

		auto* PassParameters = GraphBuilder.AllocParameters<FResampleLightingHistoryToCardCaptureAtlasCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = FrameTemporaries.LumenCardSceneUniformBuffer;
		PassParameters->DirectLightingAtlas = FrameTemporaries.DirectLightingAtlas;
		PassParameters->IndirectLightingAtlas = FrameTemporaries.IndirectLightingAtlas;
		PassParameters->RadiosityNumFramesAccumulatedAtlas = FrameTemporaries.RadiosityNumFramesAccumulatedAtlas;
		PassParameters->TileShadowDownsampleFactorAtlasForResampling = GraphBuilder.CreateSRV(FrameTemporaries.TileShadowDownsampleFactorAtlas, PF_R32G32B32A32_UINT);
		PassParameters->RWDirectLightingCardCaptureAtlas = GraphBuilder.CreateUAV(OutCardCaptureLightingAtlas.DirectLighting);
		PassParameters->RWRadiosityCardCaptureAtlas = GraphBuilder.CreateUAV(OutCardCaptureLightingAtlas.IndirectLighting);
		PassParameters->RWRadiosityNumFramesAccumulatedCardCaptureAtlas = GraphBuilder.CreateUAV(OutCardCaptureLightingAtlas.NumFramesAccumulated);
		PassParameters->RWTileShadowDownsampleFactorAtlas = GraphBuilder.CreateUAV(OutCardCaptureLightingAtlas.TileShadowDownsampleFactor, PF_R32G32B32A32_UINT);
		PassParameters->NewCardPageResampleData = CardPageResampleDataBufferSRV;
		PassParameters->NewCardTileResampleData = CardTileResampleDataBufferSRV;
		PassParameters->RectCoordBuffer = OutCardCaptureRectBufferSRV;
		PassParameters->CardCaptureAtlasWidthInTiles = LumenSceneData.GetCardCaptureAtlasSize().X / Lumen::CardTileSize;

		auto ComputeShader = View.ShaderMap->GetShader<FResampleLightingHistoryToCardCaptureAtlasCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ResampleLightingHistoryToCardCaptureAtlasCS"),
			ComputeShader,
			PassParameters,
			FIntVector(NumCaptureTiles, 1, 1));
	}
}

void FLumenSceneData::FillFrameTemporaries(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries)
{
	const auto FillBuffer = [&](FRDGBufferSRV*& OutSRV, const TRefCountPtr<FRDGPooledBuffer>& InBuffer)
	{
		if (!OutSRV && InBuffer)
		{
			OutSRV = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(InBuffer));
		}
	};

	FillBuffer(FrameTemporaries.CardBufferSRV, CardBuffer);
	FillBuffer(FrameTemporaries.MeshCardsBufferSRV, MeshCardsBuffer);
	FillBuffer(FrameTemporaries.HeightfieldBufferSRV, HeightfieldBuffer);
	FillBuffer(FrameTemporaries.PrimitiveGroupBufferSRV, PrimitiveGroupBuffer);
	FillBuffer(FrameTemporaries.SceneInstanceIndexToMeshCardsIndexBufferSRV, SceneInstanceIndexToMeshCardsIndexBuffer);
	FillBuffer(FrameTemporaries.PageTableBufferSRV, PageTableBuffer);
	FillBuffer(FrameTemporaries.CardPageBufferSRV, CardPageBuffer);

	const auto FillTexture = [&](FRDGTexture*& OutTexture, const TRefCountPtr<IPooledRenderTarget>& InTexture)
	{
		if (!OutTexture && InTexture)
		{
			OutTexture = GraphBuilder.RegisterExternalTexture(InTexture);
		}
	};

	FillTexture(FrameTemporaries.AlbedoAtlas, AlbedoAtlas);
	FillTexture(FrameTemporaries.OpacityAtlas, OpacityAtlas);
	FillTexture(FrameTemporaries.NormalAtlas, NormalAtlas);
	FillTexture(FrameTemporaries.EmissiveAtlas, EmissiveAtlas);
	FillTexture(FrameTemporaries.DepthAtlas, DepthAtlas);
	FillTexture(FrameTemporaries.DirectLightingAtlas, DirectLightingAtlas);
	FillTexture(FrameTemporaries.IndirectLightingAtlas, IndirectLightingAtlas);
	FillTexture(FrameTemporaries.RadiosityNumFramesAccumulatedAtlas, RadiosityNumFramesAccumulatedAtlas);
	FillTexture(FrameTemporaries.FinalLightingAtlas, FinalLightingAtlas);
	if (!FrameTemporaries.TileShadowDownsampleFactorAtlas && TileShadowDownsampleFactorAtlas)
	{
		FrameTemporaries.TileShadowDownsampleFactorAtlas = GraphBuilder.RegisterExternalBuffer(TileShadowDownsampleFactorAtlas);
	}
	FillTexture(FrameTemporaries.DiffuseLightingAndSecondMomentHistoryAtlas, DiffuseLightingAndSecondMomentHistoryAtlas);
	FillTexture(FrameTemporaries.NumFramesAccumulatedHistoryAtlas, NumFramesAccumulatedHistoryAtlas);
}

void FDeferredShadingSceneRenderer::BeginUpdateLumenSceneTasks(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries)
{
	LLM_SCOPE_BYTAG(Lumen);

	bool bAnyLumenActive = false;
	bool bHasOrthographicView = false;

	for (FViewInfo& View : Views)
	{
		bool bLumenActive = ShouldRenderLumenDiffuseGI(Scene, View);
		bAnyLumenActive = bAnyLumenActive || bLumenActive;
		if (!bHasOrthographicView && !View.IsPerspectiveProjection())
		{
			bHasOrthographicView = true;
		}

		if (bLumenActive)
		{
			// Cache LumenSceneData pointer per view for efficient lookup of the view specific Lumen scene (also nice for debugging)
			View.ViewLumenSceneData = Scene->FindLumenSceneData(View.ViewState ? View.ViewState->GetShareOriginViewKey() : 0, View.GPUMask.GetFirstIndex());

#if WITH_MGPU
			if (View.ViewLumenSceneData->bViewSpecific)
			{
				// Update view specific scene data if the GPU mask changed (copies resources cross GPU so CPU and GPU data are coherent)
				View.ViewLumenSceneData->UpdateGPUMask(GraphBuilder, FrameTemporaries, View.ViewState->Lumen, View.GPUMask);
			}
			else if (View.GPUMask.GetFirstIndex() != 0)
			{
				// Otherwise, if this view is on a different GPU, we need to allocate GPU specific scene data (if not already allocated)
				if (View.ViewLumenSceneData == Scene->DefaultLumenSceneData)
				{
					View.ViewLumenSceneData = new FLumenSceneData(Scene->DefaultLumenSceneData->bTrackAllPrimitives);

					View.ViewLumenSceneData->CopyInitialData(*Scene->DefaultLumenSceneData);

					// Key shouldn't already exist in Scene, because "FindLumenSceneData" above should have found it
					FLumenSceneDataKey ByGPUIndex = { 0, View.GPUMask.GetFirstIndex() };
					check(Scene->PerViewOrGPULumenSceneData.Find(ByGPUIndex) == nullptr);

					Scene->PerViewOrGPULumenSceneData.Emplace(ByGPUIndex, View.ViewLumenSceneData);
				}
			}
#endif  // WITH_MGPU
		}
	}

	LumenCardRenderer.Reset();

	// Release Lumen scene resource if it's disabled by scalability
	if (!LumenDiffuseIndirect::IsAllowed())
	{
		FLumenSceneData& LumenSceneData = *Scene->GetLumenSceneData(Views[0]);
		LumenSceneData.ReleaseAtlas();
	}

	if (!bAnyLumenActive || ViewFamily.EngineShowFlags.HitProxies)
	{
		return;
	}

	const FRHIGPUMask GPUMask = GraphBuilder.RHICmdList.GetGPUMask();
	const bool bAllowCardSharing = LumenScene::AllowSurfaceCacheCardSharing();
	const bool bAllowCardDownsampleFromSelf = CVarLumenSceneSurfaceCacheAllowCardDownsampleFromSelf.GetValueOnRenderThread() != 0;

	FLumenSceneData& LumenSceneData = *Scene->GetLumenSceneData(Views[0]);
	LumenSceneData.bDebugClearAllCachedState = GLumenSceneRecaptureLumenSceneEveryFrame != 0
		|| LumenSceneData.bAllowCardSharing != bAllowCardSharing
		|| LumenSceneData.bAllowCardDownsampleFromSelf != bAllowCardDownsampleFromSelf;
	LumenSceneData.bAllowCardSharing = bAllowCardSharing;
	LumenSceneData.bAllowCardDownsampleFromSelf = bAllowCardDownsampleFromSelf;
	FrameTemporaries.bReallocateAtlas = LumenSceneData.UpdateAtlasSize();

	FLumenSceneData::FFeedbackData SurfaceCacheFeedbackData;
	{
		extern int32 GLumenSurfaceCacheFeedback;
		if (GLumenSurfaceCacheFeedback != 0)
		{
			FrameTemporaries.SurfaceCacheFeedbackBuffer = LumenSceneData.SurfaceCacheFeedback.GetLatestReadbackBuffer();

			if (FrameTemporaries.SurfaceCacheFeedbackBuffer)
			{
				QUICK_SCOPE_CYCLE_COUNTER(LockSurfaceCacheFeedbackBuffer);
				SurfaceCacheFeedbackData.NumElements = Lumen::GetCompactedFeedbackBufferSize();
				SurfaceCacheFeedbackData.Data = (const uint32*) FrameTemporaries.SurfaceCacheFeedbackBuffer->Lock(SurfaceCacheFeedbackData.NumElements * sizeof(uint32) * Lumen::FeedbackBufferElementStride);
			}
		}
	}

	const FLumenSceneReadback::FAddOp* SceneAddOpsReadbackData = nullptr;
	const FLumenSceneReadback::FRemoveOp* SceneRemoveOpsReadbackData = nullptr;

	if (CVarLumenSceneGPUDrivenUpdate.GetValueOnRenderThread() != 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(LockSceneReadbackBuffer);

		FLumenSceneReadback::FBuffersRHI ReadbackBuffers = LumenSceneData.SceneReadback.GetLatestReadbackBuffers();

		FrameTemporaries.SceneAddOpsReadbackBuffer = ReadbackBuffers.AddOps;
		FrameTemporaries.SceneRemoveOpsReadbackBuffer = ReadbackBuffers.RemoveOps;

		if (ReadbackBuffers.AddOps)
		{
			SceneAddOpsReadbackData = (const FLumenSceneReadback::FAddOp*) FrameTemporaries.SceneAddOpsReadbackBuffer->Lock(LumenSceneData.SceneReadback.GetAddOpsBufferSizeInBytes());
		}

		if (ReadbackBuffers.RemoveOps)
		{
			SceneRemoveOpsReadbackData = (const FLumenSceneReadback::FRemoveOp*)FrameTemporaries.SceneRemoveOpsReadbackBuffer->Lock(LumenSceneData.SceneReadback.GetRemoveOpsBufferSizeInBytes());
		}			
	}

	FrameTemporaries.UpdateSceneTask = GraphBuilder.AddSetupTask([this, GPUMask, &LumenSceneData, bReallocateAtlas = FrameTemporaries.bReallocateAtlas, SurfaceCacheFeedbackData, SceneAddOpsReadbackData, SceneRemoveOpsReadbackData, bHasOrthographicView]
	{
		SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_BeginUpdateLumenSceneTasks, FColor::Emerald);
		QUICK_SCOPE_CYCLE_COUNTER(BeginUpdateLumenSceneTasks);

		// Surface cache reset for debugging
		if ((GLumenSceneSurfaceCacheReset != 0)
			|| (GLumenSceneSurfaceCacheResetEveryNthFrame > 0 && (ViewFamily.FrameNumber % (uint32)GLumenSceneSurfaceCacheResetEveryNthFrame == 0)))
		{
			LumenSceneData.bDebugClearAllCachedState = true;
			GLumenSceneSurfaceCacheReset = 0;
		}

		if (GLumenSceneForceEvictHiResPages != 0)
		{
			LumenSceneData.ForceEvictEntireCache();
			GLumenSceneForceEvictHiResPages = 0;
		}

		LumenSceneData.NumMeshCardsToAdd = 0;
		LumenSceneData.NumLockedCardsToUpdate = 0;
		LumenSceneData.NumHiResPagesToAdd = 0;

		UpdateLumenScenePrimitives(GPUMask, Scene);

		if (LumenSceneData.bDebugClearAllCachedState || bReallocateAtlas)
		{
			LumenSceneData.RemoveAllMeshCards();
		}

		TArray<FVector, TInlineAllocator<LUMEN_MAX_VIEWS>> LumenSceneCameraOrigins;
		float MaxCardUpdateDistanceFromCamera = 0.0f;
		float LumenSceneDetail = 0.0f;
		bool bAddTranslucentToCache = false;

		for (const FViewInfo& View : Views)
		{
			LumenSceneCameraOrigins.Add(Lumen::GetLumenSceneViewOrigin(View, Lumen::GetNumGlobalDFClipmaps(View) - 1));
			MaxCardUpdateDistanceFromCamera = FMath::Max(MaxCardUpdateDistanceFromCamera, LumenScene::GetCardMaxDistance(View));
			LumenSceneDetail = FMath::Max(LumenSceneDetail, FMath::Clamp<float>(View.FinalPostProcessSettings.LumenSceneDetail, .125f, 8.0f));
			bAddTranslucentToCache |= LumenReflections::UseTranslucentRayTracing(View) && LumenReflections::UseHitLighting(View, GetViewPipelineState(View).DiffuseIndirectMethod);
			bAddTranslucentToCache |= RayTracedTranslucency::IsEnabled(View);
		}

		// Add streaming view origins, only if there are futher apart than existing origins
		AddLumenStreamingViewOrigins(ViewFamily, LumenSceneCameraOrigins);

		const int32 MaxTileCapturesPerFrame = GetMaxTileCapturesPerFrame();

		if (MaxTileCapturesPerFrame > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(FillCardPagesToRender);

			TArray<FSurfaceCacheRequest, SceneRenderingAllocator> SurfaceCacheRequests;

			if (CVarLumenSceneGPUDrivenUpdate.GetValueOnRenderThread() != 0)
			{
				ProcessSceneRemoveOpsReadbackData(LumenSceneData, SceneRemoveOpsReadbackData);
				ProcessSceneAddOpsReadbackData(LumenSceneData, SceneAddOpsReadbackData);
			}
			else
			{
				UpdateSurfaceCachePrimitives(
					LumenSceneData,
					LumenSceneCameraOrigins,
					bHasOrthographicView,
					LumenSceneDetail,
					MaxCardUpdateDistanceFromCamera,
					LumenCardRenderer,
					bAddTranslucentToCache);
			}

			UpdateSurfaceCacheMeshCards(
				LumenSceneData,
				SurfaceCacheFeedbackData,
				LumenSceneCameraOrigins,
				bHasOrthographicView,
				LumenSceneDetail,
				MaxCardUpdateDistanceFromCamera,
				SurfaceCacheRequests,
				ViewFamily);

			LumenSceneData.ProcessLumenSurfaceCacheRequests(
				Views[0],
				MaxCardUpdateDistanceFromCamera,
				MaxTileCapturesPerFrame,
				LumenCardRenderer,
				GPUMask,
				SurfaceCacheRequests);
		}

		TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;

		if (CardPagesToRender.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(MeshPassSetup);

		#if (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT) && STATS
			if (GLumenSceneSurfaceCacheLogUpdates != 0)
			{
				UE_LOG(LogRenderer, Log, TEXT("Surface Cache Updates: %d"), CardPagesToRender.Num());

				if (GLumenSceneSurfaceCacheLogUpdates > 1)
				{ 
					for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
					{
						const FLumenPrimitiveGroup& LumenPrimitiveGroup = LumenSceneData.PrimitiveGroups[CardPageRenderData.PrimitiveGroupIndex];

						UE_LOG(LogRenderer, Log, TEXT("%s Instance:%d NumPrimsInGroup: %d"),
							*LumenPrimitiveGroup.Primitives[0]->Proxy->GetStatId().GetName().ToString(),
							LumenPrimitiveGroup.PrimitiveInstanceIndex,
							LumenPrimitiveGroup.Primitives.Num());
					}
				}
			}
		#endif

			for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
			{
				CardPageRenderData.StartMeshDrawCommandIndex = LumenCardRenderer.MeshDrawCommands.Num();
				CardPageRenderData.NumMeshDrawCommands = 0;
				int32 NumNanitePrimitives = 0;

				const FLumenPrimitiveGroup& PrimitiveGroup = LumenSceneData.PrimitiveGroups[CardPageRenderData.PrimitiveGroupIndex];
				const FLumenCard& Card = LumenSceneData.Cards[CardPageRenderData.CardIndex];
				ensure(Card.bVisible);

				if (PrimitiveGroup.bHeightfield)
				{
					LumenScene::AddCardCaptureDraws(
						Scene,
						CardPageRenderData,
						PrimitiveGroup,
						LumenSceneData.LandscapePrimitives,
						LumenCardRenderer.MeshDrawCommands,
						LumenCardRenderer.MeshDrawPrimitiveIds);
				}
				else
				{
					LumenScene::AddCardCaptureDraws(
						Scene,
						CardPageRenderData,
						PrimitiveGroup,
						PrimitiveGroup.Primitives,
						LumenCardRenderer.MeshDrawCommands,
						LumenCardRenderer.MeshDrawPrimitiveIds);
				}

				CardPageRenderData.NumMeshDrawCommands = LumenCardRenderer.MeshDrawCommands.Num() - CardPageRenderData.StartMeshDrawCommandIndex;
			}
		}

	}, MakeArrayView({ Scene->GetCacheMeshDrawCommandsTask(), Scene->GetCacheNaniteMaterialBinsTask() }), UE::Tasks::ETaskPriority::High);
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardScene, "LumenCardScene");

void UpdateLumenCardSceneUniformBuffer(
	FRDGBuilder& GraphBuilder,
	FScene* Scene,
	const FLumenSceneData& LumenSceneData,
	FLumenSceneFrameTemporaries& FrameTemporaries)
{
	FLumenCardScene* UniformParameters = GraphBuilder.AllocParameters<FLumenCardScene>();
	UniformParameters->NumCards = LumenSceneData.Cards.Num();
	UniformParameters->NumMeshCards = LumenSceneData.MeshCards.Num();
	UniformParameters->NumCardPages = LumenSceneData.GetNumCardPages();
	UniformParameters->NumHeightfields = LumenSceneData.Heightfields.Num();
	UniformParameters->NumPrimitiveGroups = LumenSceneData.PrimitiveGroups.Num();
	UniformParameters->PhysicalAtlasSize = LumenSceneData.GetPhysicalAtlasSize();
	UniformParameters->InvPhysicalAtlasSize = FVector2f(1.0f) / UniformParameters->PhysicalAtlasSize;
	UniformParameters->IndirectLightingAtlasDownsampleFactor = LumenRadiosity::GetAtlasDownsampleFactor();

	if (FrameTemporaries.CardBufferSRV)
	{
		UniformParameters->CardData = FrameTemporaries.CardBufferSRV;
		UniformParameters->MeshCardsData = FrameTemporaries.MeshCardsBufferSRV;
		UniformParameters->HeightfieldData = FrameTemporaries.HeightfieldBufferSRV;
		UniformParameters->PrimitiveGroupData = FrameTemporaries.PrimitiveGroupBufferSRV;
		UniformParameters->SceneInstanceIndexToMeshCardsIndexBuffer = FrameTemporaries.SceneInstanceIndexToMeshCardsIndexBufferSRV;
		UniformParameters->PageTableBuffer = FrameTemporaries.PageTableBufferSRV;
		UniformParameters->CardPageData = FrameTemporaries.CardPageBufferSRV;
	}
	else
	{
		FRDGBufferSRVRef DefaultSRV = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultStructuredBuffer(GraphBuilder, sizeof(FVector4f)));

		UniformParameters->CardData = DefaultSRV;
		UniformParameters->MeshCardsData = DefaultSRV; 
		UniformParameters->HeightfieldData = DefaultSRV;
		UniformParameters->CardPageData = DefaultSRV;
		UniformParameters->PrimitiveGroupData = DefaultSRV;
		UniformParameters->PageTableBuffer = UniformParameters->SceneInstanceIndexToMeshCardsIndexBuffer = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultByteAddressBuffer(GraphBuilder, sizeof(FVector4f)));
	}

	if (FrameTemporaries.AlbedoAtlas)
	{
		UniformParameters->AlbedoAtlas = FrameTemporaries.AlbedoAtlas;
		UniformParameters->OpacityAtlas = FrameTemporaries.OpacityAtlas;
		UniformParameters->NormalAtlas = FrameTemporaries.NormalAtlas;
		UniformParameters->EmissiveAtlas = FrameTemporaries.EmissiveAtlas;
		UniformParameters->DepthAtlas = FrameTemporaries.DepthAtlas;
	}
	else
	{
		UniformParameters->AlbedoAtlas = UniformParameters->OpacityAtlas = UniformParameters->NormalAtlas = UniformParameters->EmissiveAtlas = UniformParameters->DepthAtlas = GSystemTextures.GetBlackDummy(GraphBuilder);
	}

	FrameTemporaries.LumenCardSceneUniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
}

DECLARE_GPU_STAT(UpdateCardSceneBuffer);

class FClearLumenCardCapturePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearLumenCardCapturePS);
	SHADER_USE_PARAMETER_STRUCT(FClearLumenCardCapturePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearLumenCardCapturePS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "ClearLumenCardCapturePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FClearLumenCardCaptureParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FClearLumenCardCapturePS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void ClearLumenCardCapture(
	FRDGBuilder& GraphBuilder,
	const FGlobalShaderMap* GlobalShaderMap,
	const FCardCaptureAtlas& Atlas,
	FRDGBufferSRVRef RectCoordBufferSRV,
	uint32 NumRects)
{
	FClearLumenCardCaptureParameters* PassParameters = GraphBuilder.AllocParameters<FClearLumenCardCaptureParameters>();

	PassParameters->RenderTargets[0] = FRenderTargetBinding(Atlas.Albedo, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(Atlas.Normal, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(Atlas.Emissive, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Atlas.DepthStencil, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

	auto PixelShader = GlobalShaderMap->GetShader<FClearLumenCardCapturePS>();

	FPixelShaderUtils::AddRasterizeToRectsPass<FClearLumenCardCapturePS>(
		GraphBuilder,
		GlobalShaderMap,
		RDG_EVENT_NAME("ClearCardCapture"),
		PixelShader,
		PassParameters,
		Atlas.Size,
		RectCoordBufferSRV,
		NumRects,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_Always,
		true, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		false, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		0xff, 0xff>::GetRHI());
}

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPassParameters, )
	// An RDG View uniform buffer is used as an optimization to move creation off the render thread.
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardPassUniformParameters, CardPass)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FIntPoint FLumenSceneData::GetCardCaptureAtlasSizeInPages() const
{
	const float MultPerComponent = 1.0f / FMath::Sqrt(FMath::Clamp(GLumenSceneCardCaptureFactor, 1.0f, 1024.0f));

	FIntPoint CaptureAtlasSizeInPages;
	CaptureAtlasSizeInPages.X = FMath::DivideAndRoundUp<uint32>(PhysicalAtlasSize.X * MultPerComponent + 0.5f, Lumen::PhysicalPageSize);
	CaptureAtlasSizeInPages.Y = FMath::DivideAndRoundUp<uint32>(PhysicalAtlasSize.Y * MultPerComponent + 0.5f, Lumen::PhysicalPageSize);
	return CaptureAtlasSizeInPages;
}

FIntPoint FLumenSceneData::GetCardCaptureAtlasSize() const 
{
	return GetCardCaptureAtlasSizeInPages() * Lumen::PhysicalPageSize;
}

uint32 FLumenSceneData::GetCardCaptureRefreshNumTexels() const
{
	const float CardCaptureRefreshFraction = FMath::Clamp(CVarLumenSceneCardCaptureRefreshFraction.GetValueOnRenderThread(), 0.0f, 1.0f);
	if (CardCaptureRefreshFraction > 0.0f)
	{
		// Allow to capture at least 1 full physical page
		FIntPoint CardCaptureAtlasSize = GetCardCaptureAtlasSize();
		return FMath::Max(CardCaptureAtlasSize.X * CardCaptureAtlasSize.Y * CardCaptureRefreshFraction, Lumen::PhysicalPageSize * Lumen::PhysicalPageSize);
	}

	return 0;
}

uint32 FLumenSceneData::GetCardCaptureRefreshNumPages() const
{
	const float CardCaptureRefreshFraction = FMath::Clamp(CVarLumenSceneCardCaptureRefreshFraction.GetValueOnRenderThread(), 0.0f, 1.0f);
	if (CardCaptureRefreshFraction > 0.0f)
	{
		// Allow to capture at least 1 full physical page
		return FMath::Clamp(GetMaxTileCapturesPerFrame() * CardCaptureRefreshFraction, 1, GetMaxTileCapturesPerFrame());
	}

	return 0;
}

bool UpdateGlobalLightingState(const FScene* Scene, const FViewInfo& View, FLumenSceneData& LumenSceneData)
{
	FLumenGlobalLightingState& GlobalLightingState = LumenSceneData.GlobalLightingState;

	bool bPropagateGlobalLightingChange = false;
	const FLightSceneInfo* DirectionalLightSceneInfo = nullptr;

	for (const FLightSceneInfo* LightSceneInfo : Scene->DirectionalLights)
	{
		if (LightSceneInfo->ShouldRenderLightViewIndependent()
			&& LightSceneInfo->ShouldRenderLight(View, true)
			&& LightSceneInfo->Proxy->GetIndirectLightingScale() > 0.0f)
		{
			DirectionalLightSceneInfo = LightSceneInfo;
			break;
		}
	}

	{
		const float OldMax = GlobalLightingState.bDirectionalLightValid ? GlobalLightingState.DirectionalLightColor.GetMax() : 0.0f;
		const float NewMax = DirectionalLightSceneInfo ? DirectionalLightSceneInfo->Proxy->GetColor().GetMax() : 0.0f;
		const float Ratio = FMath::Max(OldMax, .00001f) / FMath::Max(NewMax, .00001f);

		if (Ratio > 4.0f || Ratio < .25f)
		{
			bPropagateGlobalLightingChange = true;
		}
	}

	if (DirectionalLightSceneInfo)
	{
		GlobalLightingState.DirectionalLightColor = DirectionalLightSceneInfo->Proxy->GetColor();
		GlobalLightingState.bDirectionalLightValid = true;
	}
	else
	{
		GlobalLightingState.DirectionalLightColor = FLinearColor::Black;
		GlobalLightingState.bDirectionalLightValid = false;
	}

	const FSkyLightSceneProxy* SkyLightProxy = Scene->SkyLight;

	{
		const float OldMax = GlobalLightingState.bSkyLightValid ? GlobalLightingState.SkyLightColor.GetMax() : 0.0f;
		const float NewMax = SkyLightProxy ? SkyLightProxy->GetEffectiveLightColor().GetMax() : 0.0f;
		const float Ratio = FMath::Max(OldMax, .00001f) / FMath::Max(NewMax, .00001f);

		if (Ratio > 4.0f || Ratio < .25f)
		{
			bPropagateGlobalLightingChange = true;
		}
	}

	if (SkyLightProxy)
	{
		GlobalLightingState.SkyLightColor = SkyLightProxy->GetEffectiveLightColor();
		GlobalLightingState.bSkyLightValid = true;
	}
	else
	{
		GlobalLightingState.SkyLightColor = FLinearColor::Black;
		GlobalLightingState.bSkyLightValid = false;
	}

	if (CVarLumenScenePropagateGlobalLightingChange.GetValueOnRenderThread() == 0)
	{
		bPropagateGlobalLightingChange = false;
	}

	return bPropagateGlobalLightingChange;
}

void FDeferredShadingSceneRenderer::UpdateLumenScene(FRDGBuilder& GraphBuilder, FLumenSceneFrameTemporaries& FrameTemporaries)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::UpdateLumenScene);
	CSV_SCOPED_SET_WAIT_STAT(UpdateLumenScene);

	FrameTemporaries.UpdateSceneTask.Wait();

	if (FrameTemporaries.SceneAddOpsReadbackBuffer)
	{
		FrameTemporaries.SceneAddOpsReadbackBuffer->Unlock();
	}

	if (FrameTemporaries.SceneRemoveOpsReadbackBuffer)
	{
		FrameTemporaries.SceneRemoveOpsReadbackBuffer->Unlock();
	}

	if (FrameTemporaries.SurfaceCacheFeedbackBuffer)
	{
		FrameTemporaries.SurfaceCacheFeedbackBuffer->Unlock();
	}

	bool bAnyLumenActive = false;

	for (FViewInfo& View : Views)
	{
		const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);
		bool bLumenActive =
			(ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen
				// Don't update scene lighting for secondary views
				&& !View.bIsPlanarReflection 
				&& !View.bIsReflectionCapture
				&& View.ViewState);

		bAnyLumenActive = bAnyLumenActive || bLumenActive;
	}

	if (bAnyLumenActive)
	{
		FLumenSceneData& LumenSceneData = *Scene->GetLumenSceneData(Views[0]);
		const TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;

		QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenScene);

		RHI_BREADCRUMB_EVENT_STAT(GraphBuilder.RHICmdList, UpdateLumenSceneBuffers, "UpdateLumenSceneBuffers");
		SCOPED_GPU_STAT(GraphBuilder.RHICmdList, UpdateLumenSceneBuffers);

		RDG_EVENT_SCOPE_STAT(GraphBuilder, LumenSceneUpdate, "LumenSceneUpdate: %u card captures %.3fM texels", CardPagesToRender.Num(), LumenCardRenderer.NumCardTexelsToCapture / (1024.0f * 1024.0f));
		RDG_GPU_STAT_SCOPE(GraphBuilder, LumenSceneUpdate);

		// Atlas reallocation
		if (FrameTemporaries.bReallocateAtlas || !LumenSceneData.AlbedoAtlas)
		{
			LumenSceneData.AllocateCardAtlases(GraphBuilder, FrameTemporaries, Views[0].Family);
			ClearLumenSurfaceCacheAtlas(GraphBuilder, FrameTemporaries, Views[0].ShaderMap);
		}

		LumenSceneData.FillFrameTemporaries(GraphBuilder, FrameTemporaries);

		if (LumenSceneData.bDebugClearAllCachedState)
		{
			ClearLumenSurfaceCacheAtlas(GraphBuilder, FrameTemporaries, Views[0].ShaderMap);
		}

		if (CVarLumenSceneUploadEveryFrame.GetValueOnRenderThread() != 0)
		{
			LumenSceneData.bReuploadSceneRequest = true;
		}

		UpdateLumenCardSceneUniformBuffer(GraphBuilder, Scene, *Scene->GetLumenSceneData(Views[0]), FrameTemporaries);

		FCardCaptureAtlas CardCaptureAtlas;
		FRDGBufferSRVRef CardCaptureRectBufferSRV = nullptr;
		bool bNeedsClearCaptureAtlas = false;

		if (CardPagesToRender.Num())
		{
			// Before we update the GPU page table, read from the persistent atlases for the card pages we are reallocating, and write it to the card capture atlas
			// This is a resample operation, as the original data may have been at a different mip level, or didn't exist at all
			ResampleLumenCards(
				GraphBuilder,
				Views[0],
				Scene,
				LumenSceneData,
				FrameTemporaries,
				CardPagesToRender,
				LumenCardRenderer.bHasAnyCardCopy,
				CardCaptureAtlas,
				LumenCardRenderer.ResampledCardCaptureAtlas,
				CardCaptureRectBufferSRV);
		}

		FRDGScatterUploadBuilder& UploadBuilder = *FRDGScatterUploadBuilder::Create(GraphBuilder);

		LumenSceneData.UploadPageTable(GraphBuilder, UploadBuilder, FrameTemporaries);

		LumenCardRenderer.bPropagateGlobalLightingChange = UpdateGlobalLightingState(Scene, Views[0], LumenSceneData);

		Lumen::UpdateCardSceneBuffer(GraphBuilder, UploadBuilder, FrameTemporaries, ViewFamily, Scene);

		UploadBuilder.Execute(GraphBuilder);

		if (CVarLumenSceneGPUDrivenUpdate.GetValueOnRenderThread() != 0)
		{
			LumenScene::GPUDrivenUpdate(GraphBuilder, Scene, Views, FrameTemporaries);
		}

		// Init transient render targets for capturing cards
		if (!CardCaptureAtlas.Albedo)
		{
			LumenScene::AllocateCardCaptureAtlas(GraphBuilder, LumenSceneData.GetCardCaptureAtlasSize(), CardCaptureAtlas, Scene->GetShaderPlatform());
			bNeedsClearCaptureAtlas = true;
		}

		if (CardPagesToRender.Num() > 0)
		{
			FRHIBuffer* PrimitiveIdVertexBuffer = nullptr;
			FInstanceCullingResult InstanceCullingResult;
			FInstanceCullingContext* InstanceCullingContext = nullptr;
			if (Scene->GPUScene.IsEnabled())
			{
				InstanceCullingContext = GraphBuilder.AllocObject<FInstanceCullingContext>(TEXT("LumenCardCapture"), Views[0].GetShaderPlatform(), nullptr, TArrayView<const int32>(&Views[0].SceneRendererPrimaryViewId, 1), nullptr);
				
				int32 MaxInstances = 0;
				int32 VisibleMeshDrawCommandsNum = 0;
				int32 NewPassVisibleMeshDrawCommandsNum = 0;
				
				InstanceCullingContext->SetupDrawCommands(LumenCardRenderer.MeshDrawCommands, false, Scene, MaxInstances, VisibleMeshDrawCommandsNum, NewPassVisibleMeshDrawCommandsNum);
				// Not supposed to do any compaction here.
				ensure(VisibleMeshDrawCommandsNum == LumenCardRenderer.MeshDrawCommands.Num());

				InstanceCullingContext->BuildRenderingCommands(GraphBuilder, Scene->GPUScene, Views[0].DynamicPrimitiveCollector.GetInstanceSceneDataOffset(), Views[0].DynamicPrimitiveCollector.NumInstances(), InstanceCullingResult);
			}
			else
			{
				// Prepare primitive Id VB for rendering mesh draw commands.
				if (LumenCardRenderer.MeshDrawPrimitiveIds.Num() > 0)
				{
					const uint32 PrimitiveIdBufferDataSize = LumenCardRenderer.MeshDrawPrimitiveIds.Num() * sizeof(int32);

					FPrimitiveIdVertexBufferPoolEntry Entry = GPrimitiveIdVertexBufferPool.Allocate(GraphBuilder.RHICmdList, PrimitiveIdBufferDataSize);
					PrimitiveIdVertexBuffer = Entry.BufferRHI;

					void* RESTRICT Data = GraphBuilder.RHICmdList.LockBuffer(PrimitiveIdVertexBuffer, 0, PrimitiveIdBufferDataSize, RLM_WriteOnly);
					FMemory::Memcpy(Data, LumenCardRenderer.MeshDrawPrimitiveIds.GetData(), PrimitiveIdBufferDataSize);
					GraphBuilder.RHICmdList.UnlockBuffer(PrimitiveIdVertexBuffer);

					GPrimitiveIdVertexBufferPool.ReturnToFreeList(Entry);
				}
			}

			InstanceCullingResult.Parameters.Scene = GetSceneUniforms().GetBuffer(GraphBuilder);

			if (!CardCaptureRectBufferSRV)
			{
				FRDGBufferRef CardCaptureRectBuffer = nullptr;
				FRDGUploadData<FUintVector4> CardCaptureRectArray(GraphBuilder, CardPagesToRender.Num());

				for (int32 Index = 0; Index < CardPagesToRender.Num(); Index++)
				{
					const FCardPageRenderData& CardPageRenderData = CardPagesToRender[Index];

					FUintVector4& Rect = CardCaptureRectArray[Index];
					Rect.X = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Min.X, 0);
					Rect.Y = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Min.Y, 0);
					Rect.Z = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Max.X, 0);
					Rect.W = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Max.Y, 0);
				}

				CardCaptureRectBuffer =
					CreateUploadBuffer(GraphBuilder, TEXT("Lumen.CardCaptureRects"),
						sizeof(FUintVector4), FMath::RoundUpToPowerOfTwo(CardPagesToRender.Num()),
						CardCaptureRectArray);
				CardCaptureRectBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardCaptureRectBuffer, PF_R32G32B32A32_UINT));
			}

			if (bNeedsClearCaptureAtlas)
			{
				ClearLumenCardCapture(GraphBuilder, Views[0].ShaderMap, CardCaptureAtlas, CardCaptureRectBufferSRV, CardPagesToRender.Num());
			}

			FViewInfo* SharedView = Views[0].CreateSnapshot();
			{
				SharedView->DynamicPrimitiveCollector = FGPUScenePrimitiveCollector(&GetGPUSceneDynamicContext());
				SharedView->StereoPass = EStereoscopicPass::eSSP_FULL;
				SharedView->DrawDynamicFlags = EDrawDynamicFlags::ForceLowestLOD;

				// Don't do material texture mip biasing in proxy card rendering
				SharedView->MaterialTextureMipBias = 0;

				SharedView->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>(); //TODO: remove?
				// Overrides must be send to the view uniform buffer that is accessed by Substrate when BSDFs are sanitized for instance.
				SharedView->CachedViewUniformShaderParameters->DiffuseOverrideParameter = Views[0].CachedViewUniformShaderParameters->DiffuseOverrideParameter;
				SharedView->CachedViewUniformShaderParameters->RoughnessOverrideParameter = Views[0].CachedViewUniformShaderParameters->RoughnessOverrideParameter;
				SharedView->CachedViewUniformShaderParameters->SpecularOverrideParameter = Views[0].CachedViewUniformShaderParameters->SpecularOverrideParameter;
				SharedView->CachedViewUniformShaderParameters->NormalOverrideParameter = Views[0].CachedViewUniformShaderParameters->NormalOverrideParameter;
				SharedView->CachedViewUniformShaderParameters->GameTime = Views[0].CachedViewUniformShaderParameters->GameTime;
				SharedView->CachedViewUniformShaderParameters->RealTime = Views[0].CachedViewUniformShaderParameters->RealTime;
				SharedView->CachedViewUniformShaderParameters->StateFrameIndex = Views[0].CachedViewUniformShaderParameters->StateFrameIndex;
				SharedView->CachedViewUniformShaderParameters->StateFrameIndexMod8 = Views[0].CachedViewUniformShaderParameters->StateFrameIndexMod8;
				SharedView->CachedViewUniformShaderParameters->GeneralPurposeTweak = Views[0].CachedViewUniformShaderParameters->GeneralPurposeTweak;
				SharedView->CachedViewUniformShaderParameters->GeneralPurposeTweak2 = Views[0].CachedViewUniformShaderParameters->GeneralPurposeTweak2;

				// More textures and parameters needed by Substrate
				SharedView->CachedViewUniformShaderParameters->bShadingEnergyConservation	= Views[0].CachedViewUniformShaderParameters->bShadingEnergyConservation;
				SharedView->CachedViewUniformShaderParameters->bShadingEnergyPreservation	= Views[0].CachedViewUniformShaderParameters->bShadingEnergyPreservation;
				SharedView->CachedViewUniformShaderParameters->ShadingEnergyDiffuseTexture	= Views[0].CachedViewUniformShaderParameters->ShadingEnergyDiffuseTexture;
				SharedView->CachedViewUniformShaderParameters->ShadingEnergyClothSpecTexture= Views[0].CachedViewUniformShaderParameters->ShadingEnergyClothSpecTexture;
				SharedView->CachedViewUniformShaderParameters->ShadingEnergyGGXGlassTexture	= Views[0].CachedViewUniformShaderParameters->ShadingEnergyGGXGlassTexture;
				SharedView->CachedViewUniformShaderParameters->ShadingEnergyGGXSpecTexture	= Views[0].CachedViewUniformShaderParameters->ShadingEnergyGGXSpecTexture;
				SharedView->CachedViewUniformShaderParameters->ShadingEnergySampler			= Views[0].CachedViewUniformShaderParameters->ShadingEnergySampler;
				SharedView->CachedViewUniformShaderParameters->SimpleVolumeTexture			= Views[0].CachedViewUniformShaderParameters->SimpleVolumeTexture;
				SharedView->CachedViewUniformShaderParameters->SimpleVolumeTextureSampler	= Views[0].CachedViewUniformShaderParameters->SimpleVolumeTextureSampler;
				SharedView->CachedViewUniformShaderParameters->SimpleVolumeEnvTexture		= Views[0].CachedViewUniformShaderParameters->SimpleVolumeEnvTexture;
				SharedView->CachedViewUniformShaderParameters->SimpleVolumeEnvTextureSampler= Views[0].CachedViewUniformShaderParameters->SimpleVolumeEnvTextureSampler;

				// Copy atmospheric light data that could be used in emissive materials. Some setups apparently uses that to inject extra light bounce from atmospheric lights, accounting for atsmosphere transmittance.
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightDirection[0] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightDirection[0];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightDirection[1] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightDirection[1];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOnGroundPostTransmittance[0] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOnGroundPostTransmittance[0];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOnGroundPostTransmittance[1] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOnGroundPostTransmittance[1];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOuterSpace[0] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOuterSpace[0];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOuterSpace[1] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightIlluminanceOuterSpace[1];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightDiscLuminance[0] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightDiscLuminance[0];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightDiscLuminance[1] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightDiscLuminance[1];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightDiscCosHalfApexAngle_PPTrans[0] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightDiscCosHalfApexAngle_PPTrans[0];
				SharedView->CachedViewUniformShaderParameters->AtmosphereLightDiscCosHalfApexAngle_PPTrans[1] = Views[0].CachedViewUniformShaderParameters->AtmosphereLightDiscCosHalfApexAngle_PPTrans[1];
				SharedView->CachedViewUniformShaderParameters->SkyViewLutSizeAndInvSize = Views[0].CachedViewUniformShaderParameters->SkyViewLutSizeAndInvSize;
				SharedView->CachedViewUniformShaderParameters->SkyCameraTranslatedWorldOrigin = Views[0].CachedViewUniformShaderParameters->SkyCameraTranslatedWorldOrigin;
				SharedView->CachedViewUniformShaderParameters->SkyPlanetTranslatedWorldCenterAndViewHeight = Views[0].CachedViewUniformShaderParameters->SkyPlanetTranslatedWorldCenterAndViewHeight;
				SharedView->CachedViewUniformShaderParameters->SkyViewLutReferential = Views[0].CachedViewUniformShaderParameters->SkyViewLutReferential;
				SharedView->CachedViewUniformShaderParameters->SkyAtmosphereSkyLuminanceFactor = Views[0].CachedViewUniformShaderParameters->SkyAtmosphereSkyLuminanceFactor;
				SharedView->CachedViewUniformShaderParameters->SkyAtmospherePresentInScene = Views[0].CachedViewUniformShaderParameters->SkyAtmospherePresentInScene;
				SharedView->CachedViewUniformShaderParameters->SkyAtmosphereHeightFogContribution = Views[0].CachedViewUniformShaderParameters->SkyAtmosphereHeightFogContribution;
				SharedView->CachedViewUniformShaderParameters->SkyAtmosphereBottomRadiusKm = Views[0].CachedViewUniformShaderParameters->SkyAtmosphereBottomRadiusKm;
				SharedView->CachedViewUniformShaderParameters->SkyAtmosphereTopRadiusKm = Views[0].CachedViewUniformShaderParameters->SkyAtmosphereTopRadiusKm;
				if (ShouldRenderSkyAtmosphere(Scene, SharedView->Family->EngineShowFlags))
				{
					if (FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo())
					{
						SharedView->CachedViewUniformShaderParameters->TransmittanceLutTexture = SkyInfo->GetTransmittanceLutTexture()->GetRHI();
						SharedView->CachedViewUniformShaderParameters->TransmittanceLutTextureSampler = Views[0].CachedViewUniformShaderParameters->TransmittanceLutTextureSampler;

						SharedView->CachedViewUniformShaderParameters->DistantSkyLightLutBufferSRV = SkyInfo->GetDistantSkyLightLutBufferSRV();
						SharedView->CachedViewUniformShaderParameters->MobileDistantSkyLightLutBufferSRV = SkyInfo->GetMobileDistantSkyLightLutBufferSRV();

						// SkyViewLutTexture and CameraAerialPerspectiveVolume textures are not available at this stage. 
						// Those are also view dependent so there is no reason to make them available for Lumen card.
					}
				}

				VirtualTexture::FFeedbackShaderParams VirtualTextureFeedbackShaderParams;
				VirtualTexture::GetFeedbackShaderParams(VirtualTextureFeedbackShaderParams);
				VirtualTexture::UpdateViewUniformShaderParameters(VirtualTextureFeedbackShaderParams, *SharedView->CachedViewUniformShaderParameters);

				SharedView->CreateViewUniformBuffers(*SharedView->CachedViewUniformShaderParameters);
			}

			FLumenCardPassUniformParameters* PassUniformParameters = GraphBuilder.AllocParameters<FLumenCardPassUniformParameters>();
			SetupSceneTextureUniformParameters(GraphBuilder, &GetActiveSceneTextures(), Scene->GetFeatureLevel(), /*SceneTextureSetupMode*/ ESceneTextureSetupMode::None, PassUniformParameters->SceneTextures);
			PassUniformParameters->EyeAdaptationBuffer = GraphBuilder.CreateSRV(GetEyeAdaptationBuffer(GraphBuilder, Views[0]));
			PassUniformParameters->CachedLightingPreExposure = Lumen::GetCachedLightingPreExposure();

			{
				uint32 NumPages = 0;
				uint32 NumDraws = 0;
				uint32 NumInstances = 0;
				uint32 NumTris = 0;

				// Compute some stats about non Nanite meshes which are captured
				#if RDG_EVENTS != RDG_EVENTS_NONE
				{
					for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
					{
						if (!CardPageRenderData.NeedsRender())
						{
							continue;
						}

						if (CardPageRenderData.NumMeshDrawCommands > 0)
						{
							NumPages += 1;
							NumDraws += CardPageRenderData.NumMeshDrawCommands;

							for (int32 DrawCommandIndex = CardPageRenderData.StartMeshDrawCommandIndex; DrawCommandIndex < CardPageRenderData.StartMeshDrawCommandIndex + CardPageRenderData.NumMeshDrawCommands; ++DrawCommandIndex)
							{
								const FVisibleMeshDrawCommand& VisibleDrawCommand = LumenCardRenderer.MeshDrawCommands[DrawCommandIndex];
								const FMeshDrawCommand* MeshDrawCommand = VisibleDrawCommand.MeshDrawCommand;

								uint32 NumInstancesPerDraw = 0;

								// Count number of instances to draw
								if (VisibleDrawCommand.NumRuns)
								{
									for (int32 InstanceRunIndex = 0; InstanceRunIndex < VisibleDrawCommand.NumRuns; ++InstanceRunIndex)
									{
										const int32 FirstInstance = VisibleDrawCommand.RunArray[InstanceRunIndex * 2 + 0];
										const int32 LastInstance = VisibleDrawCommand.RunArray[InstanceRunIndex * 2 + 1];
										NumInstancesPerDraw += LastInstance - FirstInstance + 1;
									}
								}
								else
								{
									NumInstancesPerDraw += MeshDrawCommand->NumInstances;
								}

								NumInstances += NumInstancesPerDraw;
								NumTris += MeshDrawCommand->NumPrimitives * NumInstancesPerDraw;
							}
						}
					}
				}
				#endif

				QUICK_SCOPE_CYCLE_COUNTER(CardPageRenderPasses);

				FLumenCardPassParameters* CommonPassParameters = GraphBuilder.AllocParameters<FLumenCardPassParameters>();
				CommonPassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);
				CommonPassParameters->RenderTargets[0] = FRenderTargetBinding(CardCaptureAtlas.Albedo, ERenderTargetLoadAction::ELoad);
				CommonPassParameters->RenderTargets[1] = FRenderTargetBinding(CardCaptureAtlas.Normal, ERenderTargetLoadAction::ELoad);
				CommonPassParameters->RenderTargets[2] = FRenderTargetBinding(CardCaptureAtlas.Emissive, ERenderTargetLoadAction::ELoad);
				CommonPassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(CardCaptureAtlas.DepthStencil, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);

				InstanceCullingResult.GetDrawParameters(CommonPassParameters->InstanceCullingDrawParams);

				for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
				{
					if (!CardPageRenderData.NeedsRender())
					{
						continue;
					}

					RDG_EVENT_SCOPE(GraphBuilder, "MeshCardCapture Pages:%u Draws:%u Instances:%u Tris:%u", NumPages, NumDraws, NumInstances, NumTris);

					if (CardPageRenderData.NumMeshDrawCommands > 0)
					{
						CardPageRenderData.PatchView(Scene, SharedView);

						FLumenCardPassParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardPassParameters>(CommonPassParameters);
						PassParameters->View = GraphBuilder.CreateUniformBuffer(GraphBuilder.AllocParameters(SharedView->CachedViewUniformShaderParameters.Get()));

						GraphBuilder.AddPass(
							RDG_EVENT_NAME("CardPage Commands:%u", CardPageRenderData.NumMeshDrawCommands),
							PassParameters,
							ERDGPassFlags::Raster,
							[this, ShaderPlatform = Scene->GetShaderPlatform(), bGPUSceneEnabled = Scene->GPUScene.IsEnabled(), PrimitiveIdVertexBuffer, &CardPageRenderData, PassParameters, InstanceCullingContext](FRDGAsyncTask, FRHICommandList& RHICmdList)
						{
							QUICK_SCOPE_CYCLE_COUNTER(MeshPass);

							const FIntRect ViewRect = CardPageRenderData.CardCaptureAtlasRect;
							RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

							FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
							if (bGPUSceneEnabled)
							{
								FInstanceCullingDrawParams& InstanceCullingDrawParams = PassParameters->InstanceCullingDrawParams;

								InstanceCullingContext->SubmitDrawCommands(
									LumenCardRenderer.MeshDrawCommands,
									GraphicsMinimalPipelineStateSet,
									GetMeshDrawCommandOverrideArgs(PassParameters->InstanceCullingDrawParams),
									CardPageRenderData.StartMeshDrawCommandIndex,
									CardPageRenderData.NumMeshDrawCommands,
									1,
									RHICmdList);
							}
							else
							{
								FMeshDrawCommandSceneArgs SceneArgs;
								SceneArgs.PrimitiveIdsBuffer = PrimitiveIdVertexBuffer;
								
								SubmitMeshDrawCommandsRange(
									LumenCardRenderer.MeshDrawCommands,
									GraphicsMinimalPipelineStateSet,
									SceneArgs,
									FInstanceCullingContext::GetInstanceIdBufferStride(ShaderPlatform),
									false,
									CardPageRenderData.StartMeshDrawCommandIndex,
									CardPageRenderData.NumMeshDrawCommands,
									1,
									RHICmdList);
							}
						});
					}
				}
			}

			bool bAnyNaniteMeshes = false;

			for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
			{
				if (CardPageRenderData.HasNanite() && CardPageRenderData.NeedsRender())
				{
					bAnyNaniteMeshes = true;
					break;
				}
			}

			if (UseNanite(ShaderPlatform) && ViewFamily.EngineShowFlags.NaniteMeshes && bAnyNaniteMeshes)
			{
				QUICK_SCOPE_CYCLE_COUNTER(NaniteMeshPass);

				// Should have launched earlier in the frame, but ensure we have built Lumen commands here just in case (launched early will make this a no-op)
				Nanite::BuildShadingCommands(GraphBuilder, *Scene, ENaniteMeshPass::LumenCardCapture, Scene->NaniteShadingCommands[ENaniteMeshPass::LumenCardCapture]);

				const FIntPoint DepthStencilAtlasSize = CardCaptureAtlas.Size;
				const FIntRect DepthAtlasRect = FIntRect(0, 0, DepthStencilAtlasSize.X, DepthStencilAtlasSize.Y);

				Nanite::FSharedContext SharedContext{};
				SharedContext.FeatureLevel = Scene->GetFeatureLevel();
				SharedContext.ShaderMap = GetGlobalShaderMap(SharedContext.FeatureLevel);
				SharedContext.Pipeline = Nanite::EPipeline::Lumen;

				Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(
					GraphBuilder,
					SharedContext,
					ViewFamily,
					DepthStencilAtlasSize,
					DepthAtlasRect,
					Nanite::EOutputBufferMode::VisBuffer,
					true,
					/*bAsyncCompute*/ CVarLumenSceneSurfaceCacheNaniteAsyncRasterization.GetValueOnRenderThread() != 0,
					CardCaptureRectBufferSRV,
					CardPagesToRender.Num());

				Nanite::FConfiguration CullingConfig = { 0 };
				CullingConfig.bSupportsMultiplePasses	= true;
				CullingConfig.SetViewFlags(*SharedView);
				CullingConfig.bIsLumenCapture = true;
				CullingConfig.bDisableProgrammable = true;

				auto NaniteRenderer = Nanite::IRenderer::Create(
					GraphBuilder,
					*Scene,
					*SharedView,
					GetSceneUniforms(),
					SharedContext,
					RasterContext,
					CullingConfig,
					FIntRect(),
					nullptr
				);

				Nanite::FRasterResults RasterResults;

				const uint32 NumCardPagesToRender = CardPagesToRender.Num();

				uint32 NextCardIndex = 0;
				while(NextCardIndex < NumCardPagesToRender)
				{
					TArray<int32, SceneRenderingAllocator> CardPagesToCreatePackedView;
					TArray<Nanite::FInstanceDraw, SceneRenderingAllocator> NaniteInstanceDraws;

					while(NextCardIndex < NumCardPagesToRender && CardPagesToCreatePackedView.Num() < NANITE_MAX_VIEWS_PER_CULL_RASTERIZE_PASS)
					{
						const FCardPageRenderData& CardPageRenderData = CardPagesToRender[NextCardIndex];

						if (CardPageRenderData.NaniteInstanceIds.Num() > 0 && CardPageRenderData.NeedsRender())
						{
							for(uint32 InstanceID : CardPageRenderData.NaniteInstanceIds)
							{
								NaniteInstanceDraws.Add(Nanite::FInstanceDraw { InstanceID, (uint32)CardPagesToCreatePackedView.Num() });
							}

							CardPagesToCreatePackedView.Add(NextCardIndex);
						}

						NextCardIndex++;
					}

					if (NaniteInstanceDraws.Num() > 0)
					{
						RDG_EVENT_SCOPE(GraphBuilder, "Nanite::RasterizeLumenCards");

						const uint32 NumPrimaryViews = CardPagesToCreatePackedView.Num();

						Nanite::FPackedViewArray* NaniteViews = Nanite::FPackedViewArray::CreateWithSetupTask(
							GraphBuilder,
							NumPrimaryViews,
							[CardPagesToCreatePackedView = MoveTemp(CardPagesToCreatePackedView), &CardPagesToRender, NumCardPagesToRender, DepthStencilAtlasSize] (Nanite::FPackedViewArray::ArrayType& OutViews)
						{
							QUICK_SCOPE_CYCLE_COUNTER(CreateLumenPackedViews);
		
							for (const int32 CardPageToRenderIndex : CardPagesToCreatePackedView)
							{
								const FCardPageRenderData& CardPageRenderData = CardPagesToRender[CardPageToRenderIndex];

								Nanite::FPackedViewParams Params;
								Params.ViewMatrices = CardPageRenderData.ViewMatrices;
								Params.PrevViewMatrices = CardPageRenderData.ViewMatrices;
								Params.ViewRect = CardPageRenderData.CardCaptureAtlasRect;
								Params.RasterContextSize = DepthStencilAtlasSize;
								Params.MaxPixelsPerEdgeMultipler = 1.0f / CardPageRenderData.NaniteLODScaleFactor;

								OutViews.Add(Nanite::CreatePackedView(Params));
							}
						});

						NaniteRenderer->DrawGeometry(
							Scene->NaniteRasterPipelines[ENaniteMeshPass::LumenCardCapture],
							RasterResults.VisibilityQuery,
							*NaniteViews,
							NaniteInstanceDraws
						);
					}
				}

				NaniteRenderer->ExtractResults( RasterResults );

				if (CVarLumenSceneSurfaceCacheNaniteMultiView.GetValueOnRenderThread() != 0)
				{
					DispatchLumenMeshCapturePass(
						GraphBuilder,
						*Scene,
						SharedView,
						TArrayView<const FCardPageRenderData>(CardPagesToRender),
						RasterResults,
						RasterContext,
						PassUniformParameters,
						CardCaptureRectBufferSRV,
						CardPagesToRender.Num(),
						CardCaptureAtlas.Size,
						CardCaptureAtlas.Albedo,
						CardCaptureAtlas.Normal,
						CardCaptureAtlas.Emissive,
						CardCaptureAtlas.DepthStencil
					);
				}
				else
				{
					// Single capture per card. Slow path, only for debugging.
					for (int32 PageIndex = 0; PageIndex < CardPagesToRender.Num(); ++PageIndex)
					{
						if (CardPagesToRender[PageIndex].HasNanite())
						{
							DispatchLumenMeshCapturePass(
								GraphBuilder,
								*Scene,
								SharedView,
								TArrayView<const FCardPageRenderData>(&CardPagesToRender[PageIndex], 1),
								RasterResults,
								RasterContext,
								PassUniformParameters,
								CardCaptureRectBufferSRV,
								CardPagesToRender.Num(),
								CardCaptureAtlas.Size,
								CardCaptureAtlas.Albedo,
								CardCaptureAtlas.Normal,
								CardCaptureAtlas.Emissive,
								CardCaptureAtlas.DepthStencil
							);
						}
					}
				}
			}

			UpdateLumenSurfaceCacheAtlas(
				GraphBuilder,
				Views[0],
				FrameTemporaries,
				CardPagesToRender,
				CardCaptureRectBufferSRV,
				CardCaptureAtlas,
				LumenCardRenderer.ResampledCardCaptureAtlas);
		}
	}

 	UpdateLumenCardSceneUniformBuffer(GraphBuilder, Scene, *Scene->GetLumenSceneData(Views[0]), FrameTemporaries);
 
 	// Reset arrays, but keep allocated memory for 1024 elements
	int32 LumenSlack = bAnyLumenActive ? 1024 : 0;
	FLumenSceneData& LumenSceneData = *Scene->GetLumenSceneData(Views[0]);

	// Refresh LumenScene if some updates were ignored due to Lumen being inactive
	// For scene captures, don't trigger a reupload unless the scene capture has view specific LumenSceneData (not the default data)
	if (!bAnyLumenActive && (!Views[0].bIsSceneCapture || &LumenSceneData != Scene->DefaultLumenSceneData))
	{
		if (LumenSceneData.CardIndicesToUpdateInBuffer.Num() > 0
			|| LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Num() > 0
			|| LumenSceneData.HeightfieldIndicesToUpdateInBuffer.Num() > 0
			|| LumenSceneData.PrimitivesToUpdateMeshCards.Num() > 0
			|| LumenSceneData.PrimitiveGroupIndicesToUpdateInBuffer.Num() > 0
			|| LumenSceneData.PageTableIndicesToUpdateInBuffer.Num() > 0)
		{
			LumenSceneData.bReuploadSceneRequest = true;
		}
	}

	GraphBuilder.AddPostExecuteCallback([&LumenSceneData, LumenSlack]
	{
		LumenSceneData.CardIndicesToUpdateInBuffer.Empty(LumenSlack);
		LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Empty(LumenSlack);
		LumenSceneData.HeightfieldIndicesToUpdateInBuffer.Empty(LumenSlack);
		LumenSceneData.PrimitivesToUpdateMeshCards.Empty(LumenSlack);
		LumenSceneData.PrimitiveGroupIndicesToUpdateInBuffer.Empty(LumenSlack);
		LumenSceneData.PageTableIndicesToUpdateInBuffer.Empty(LumenSlack);
	});
}

void FLumenViewOrigin::Init(const FViewInfo& View)
{
	Family = View.Family;

	LumenSceneViewOrigin = Lumen::GetLumenSceneViewOrigin(View, Lumen::GetNumGlobalDFClipmaps(View) - 1);
	WorldCameraOrigin = FVector4f((FVector3f)View.ViewMatrices.GetViewOrigin(), 0.0f); // LUMEN_LWC_TODO
	ViewToClip = FMatrix44f(View.ViewMatrices.GetViewProjectionMatrix());
	PreViewTranslationDF = FDFVector3{ View.ViewMatrices.GetPreViewTranslation() };
	FrustumTranslatedWorldToClip = FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix());
	OrthoMaxDimension = View.ViewMatrices.GetOrthoDimensions().GetMax();			// Returns zero if not orthographic
	LastEyeAdaptationExposure = View.GetLastEyeAdaptationExposure();
	if (LastEyeAdaptationExposure <= 0.f)
	{
		// InverseExposureLerp() returns NaN if fed a non-positive exposure value
		LastEyeAdaptationExposure = 1.f;
	}
	MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
	CardMaxDistance = LumenScene::GetCardMaxDistance(View);
	LumenSceneDetail = FMath::Clamp<float>(View.FinalPostProcessSettings.LumenSceneDetail, .125f, 8.0f);

	ReferenceView = &View;
}

FLumenSceneFrameTemporaries::FLumenSceneFrameTemporaries(const TArray<FViewInfo>& Views)
{
	if (Views[0].bIsSceneCaptureCube)
	{
		// Cube captures use a single origin
		ViewOrigins.SetNum(1);
		ViewOrigins[0].Init(Views[0]);

		// Cube captures are omnidirectional, so we want a matrix that will pass anything as in-frustum.  An all zero matrix
		// will produce a clip position of [0,0,0,1] for any input vector, accomplishing that goal.
		FVector3f ZeroVector(ForceInitToZero);
		ViewOrigins[0].FrustumTranslatedWorldToClip = FMatrix44f(ZeroVector, ZeroVector, ZeroVector, ZeroVector);
	}
	else if (IStereoRendering::IsStereoEyeView(Views[0]))
	{
		// Stereo views can share the same origin with Primary one due to their closeness
		ViewOrigins.SetNum(1);
		ViewOrigins[0].Init(*Views[0].GetPrimaryView());
	}
	else
	{
		// Limit the number of view origin to LUMEN_MAX_VIEWS to avoid issue down the pipe.
		const int32 ViewCount = FMath::Min(LUMEN_MAX_VIEWS, Views.Num());
		ViewOrigins.SetNum(ViewCount);
		for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
		{
			ViewOrigins[ViewIndex].Init(Views[ViewIndex]);
		}
	}

	// Actual extent of viewports -- useful for passing to EncloseVisualizeExtent (used by VisualizeTexture debug feature)
	ViewExtent = FIntPoint(0,0);
	for (const FViewInfo& View : Views)
	{
		ViewExtent.X = FMath::Max(ViewExtent.X, View.ViewRect.Max.X);
		ViewExtent.Y = FMath::Max(ViewExtent.Y, View.ViewRect.Max.Y);
	}
}

FRDGTextureRef FLumenSharedRT::CreateSharedRT(
	FRDGBuilder& Builder,
	const FRDGTextureDesc& Desc,
	FIntPoint VisibleExtent,
	const TCHAR* Name,
	ERDGTextureFlags Flags)
{
	if (RenderTarget)
	{
		check(Desc.Extent == RenderTarget->Desc.Extent);
		return RenderTarget;
	}

	RenderTarget = Builder.CreateTexture(Desc, Name, Flags);
	RenderTarget->EncloseVisualizeExtent(VisibleExtent);

	return RenderTarget;
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneView.h"
#include "ShaderParameterMacros.h"
#include "GlobalShader.h"
#include "UnifiedBuffer.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "Rendering/NaniteResources.h"
#include "NaniteFeedback.h"
#include "MaterialDomain.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "Misc/ScopeRWLock.h"
#include "Experimental/Containers/RobinHoodHashTable.h"
#include "LightMapRendering.h" // TODO: Remove with later refactor (moving Nanite shading into its own files)
#include "RenderUtils.h"
#include "PrimitiveViewRelevance.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNanite, Warning, All);

DECLARE_GPU_STAT_NAMED_EXTERN(NaniteDebug, TEXT("Nanite Debug"));

struct FSceneTextures;
struct FDBufferTextures;

namespace Nanite
{

// Counterpart to FPackedNaniteView in NanitePackedNaniteView.ush
struct FPackedView
{
	FMatrix44f	SVPositionToTranslatedWorld;
	FMatrix44f	ViewToTranslatedWorld;

	FMatrix44f	TranslatedWorldToView;
	FMatrix44f	TranslatedWorldToClip;
	FMatrix44f	ViewToClip;
	FMatrix44f	ClipToRelativeWorld;

	FMatrix44f	PrevTranslatedWorldToView;
	FMatrix44f	PrevTranslatedWorldToClip;
	FMatrix44f	PrevViewToClip;
	FMatrix44f	PrevClipToRelativeWorld;

	FIntVector4	ViewRect;
	FVector4f	ViewSizeAndInvSize;
	FVector4f	ClipSpaceScaleOffset;
	FVector4f   MaterialCacheUnwrapMinAndInvSize;
	FVector4f   MaterialCachePageAdvanceAndInvCount;
	FVector3f	PreViewTranslationHigh;
	float		ViewOriginHighX;
	FVector3f	PrevPreViewTranslationHigh;
	float		ViewOriginHighY;
	FVector3f	PrevPreViewTranslationLow;
	float		CullingViewMinRadiusTestFactorSq;
	FVector3f	ViewOriginLow;
	float		ViewOriginHighZ;
	FVector3f	CullingViewOriginTranslatedWorld;
	float		RangeBasedCullingDistance;
	FVector3f	ViewForward;
	float		NearPlane;

	FVector4f	TranslatedGlobalClipPlane;

	FVector3f	PreViewTranslationLow;
	float		CullingViewScreenMultipleSq;

	FVector2f	LODScales;
	uint32		InstanceOcclusionQueryMask;
	uint32		StreamingPriorityCategory_AndFlags;

	FIntVector4 TargetLayerIdX_AndMipLevelY_AndNumMipLevelsZ;

	FIntVector4	HZBTestViewRect;	// In full resolution

	FUintVector4	FirstPersonTransformRowsExceptRow2Z; // Packed into half floats
	uint32			FirstPersonTransformRow2Z;
	uint32			LightingChannelMask;
	int32			SceneRendererPrimaryViewId; // The primary view ID either refers to this view itself, OR it refers (in the case of a secondsry, shadow view mostly) to the relevant primary view.
	uint32			Padding1;
	FVector2f		DynamicDepthCullRange;
	uint32			Padding2[2];
	

	

	/**
	 * Calculates the LOD scales assuming view size and projection is already set up.
	 * TODO: perhaps more elegant/robust if this happened at construction time, and input was a non-packed NaniteView.
	 * Note: depends on the global 'GNaniteMaxPixelsPerEdge'.
	 */
	void UpdateLODScales(const float NaniteMaxPixelsPerEdge, const float MinPixelsPerEdgeHW);
};

class FPackedViewArray
{
public:
	using ArrayType = TArray<FPackedView, SceneRenderingAllocator>;
	using TaskLambdaType = TFunction<void(ArrayType&)>;

	/** Creates a packed view array for a single element. */
	static FPackedViewArray* Create(FRDGBuilder& GraphBuilder, const FPackedView& View);

	/** Creates a packed view array for an existing array. */
	static FPackedViewArray* Create(FRDGBuilder& GraphBuilder, uint32 NumViews, ArrayType&& Views);

	/** Creates a packed view array by launching an RDG setup task. */
	static FPackedViewArray* CreateWithSetupTask(FRDGBuilder& GraphBuilder, uint32 NumViews, TaskLambdaType&& TaskLambda, UE::Tasks::FPipe* Pipe = nullptr, bool bExecuteInTask = true);

	/** Returns the view array, and will sync the setup task if one exists. */
	const ArrayType& GetViews() const
	{
		SetupTask.Wait();
		check(Views.Num() == NumViews);
		return Views;
	}

	const uint32 NumViews;

	UE::Tasks::FTask GetSetupTask() const
	{
		return SetupTask;
	}

private:
	FPackedViewArray(uint32 InNumViews)
		: NumViews(InNumViews)
	{}

	// Packed views containing all expanded mips.
	ArrayType Views;

	// The task that is generating the Views data array, if any.
	mutable UE::Tasks::FTask SetupTask;

	RDG_FRIEND_ALLOCATOR_FRIEND(FPackedViewArray);
};

struct FPackedViewParams
{
	FViewMatrices ViewMatrices;
	FViewMatrices PrevViewMatrices;
	FIntRect ViewRect;
	FIntPoint RasterContextSize;
	uint32 StreamingPriorityCategory = 0;
	float MinBoundsRadius = 0.0f;
	float LODScaleFactor = 1.0f;
	float ViewLODDistanceFactor = 1.0f;
	uint32 Flags = NANITE_VIEW_FLAG_NEAR_CLIP;

	int32 TargetLayerIndex = INDEX_NONE;
	int32 PrevTargetLayerIndex = INDEX_NONE;
	int32 TargetMipLevel = 0;
	int32 TargetMipCount = 1;

	float RangeBasedCullingDistance = 0.0f; // not used unless the flag NANITE_VIEW_FLAG_DISTANCE_CULL is set

	FIntRect HZBTestViewRect = {0, 0, 0, 0};

	float MaxPixelsPerEdgeMultipler = 1.0f;

	bool bUseCullingViewOverrides = false;
	FVector CullingViewOrigin = FVector::ZeroVector;
	float CullingViewScreenMultipleSq = 0.0f;
	float CullingViewMinRadiusTestFactorSq = 0.0f;  // not used unless the flag NANITE_VIEW_MIN_SCREEN_RADIUS_CULL is set and support is compiled into the culling shader

	FPlane GlobalClippingPlane = {0.0f, 0.0f, 0.0f, 0.0f};

	// Identifies the bit in the GPUScene::InstanceVisibilityMaskBuffer associated with the current view.
	// Visibility mask buffer may be used if this is non-zero.
	uint32 InstanceOcclusionQueryMask = 0;
	uint32 LightingChannelMask = 0b111; // All channels are visible by default
	bool bUseLightingChannelMask = false;

	int32 SceneRendererPrimaryViewId = -1;
	// clip-space Far/Near extra culling range for dynamic geometry (for VSM)
	// discards geometry that fails the culling test See FBoxCull::Frustum
	// Defaults to (0.0f, FLT_MAX) which means no extra culling 
	FVector2f DynamicDepthCullRange = FVector2f(0.0f, FLT_MAX);
};

// Helper function to setup the overrides for a culling view. 
// This is used for shadow views that have an associated "main" view that drives distance/screensize elements of the culling.
void SetCullingViewOverrides(FViewInfo const* InCullingView, Nanite::FPackedViewParams& InOutParams);

FPackedView CreatePackedView(const FPackedViewParams& Params);

// Convenience function to pull relevant packed view parameters out of a FViewInfo
FPackedView CreatePackedViewFromViewInfo(
	const FViewInfo& View,
	FIntPoint RasterContextSize,
	uint32 Flags,
	uint32 StreamingPriorityCategory = 0,
	float MinBoundsRadius = 0.0f,
	float MaxPixelsPerEdgeMultipler = 1.0f,
	/** Note: this rect should be in HZB space. */
	const FIntRect* InHZBTestViewRect = nullptr
);

/** Whether to draw multiple FSceneView in one Nanite pass (as opposed to view by view). */
bool ShouldDrawSceneViewsInOneNanitePass(const FViewInfo& View);

struct FVisualizeResult
{
	FRDGTextureRef ModeOutput;
	FName ModeName;
	int32 ModeID;
	uint8 bCompositeScene : 1;
	uint8 bSkippedTile    : 1;
};

struct FBinningData
{
	uint32 BinCount = 0;

	FRDGBufferRef DataBuffer = nullptr;
	FRDGBufferRef MetaBuffer = nullptr;
	FRDGBufferRef IndirectArgs = nullptr;
};

struct FNodesAndClusterBatchesBuffer
{
	TRefCountPtr<FRDGPooledBuffer> Buffer;
	uint32 NumNodes = 0;
	uint32 NumClusterBatches = 0;
};

/*
 * GPU side buffers containing Nanite resource data.
 */
class FGlobalResources : public FRenderResource
{
public:
	struct PassBuffers
	{
		// Used for statistics
		TRefCountPtr<FRDGPooledBuffer> StatsRasterizeArgsSWHWBuffer;
	};

	// Used for statistics
	uint32 StatsRenderFlags = 0;
	uint32 StatsDebugFlags = 0;

	const int32 MaxPickingBuffers = 4;
	int32 PickingBufferWriteIndex = 0;
	int32 PickingBufferNumPending = 0;
	TArray<TUniquePtr<FRHIGPUBufferReadback>> PickingBuffers;

	FNodesAndClusterBatchesBuffer	MainAndPostNodesAndClusterBatchesBuffer;

public:
	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	void Update(FRDGBuilder& GraphBuilder); // Called once per frame before any Nanite rendering has occurred.

	static uint32 GetMaxCandidateClusters();
	static uint32 GetMaxClusterBatches();
	static uint32 GetMaxVisibleClusters();
	static uint32 GetMaxNodes();
	static uint32 GetMaxCandidatePatches();
	static uint32 GetMaxVisiblePatches();

	inline PassBuffers& GetMainPassBuffers() { return MainPassBuffers; }
	inline PassBuffers& GetPostPassBuffers() { return PostPassBuffers; }

	TRefCountPtr<FRDGPooledBuffer>&  GetStatsBufferRef() { return StatsBuffer; }
	TRefCountPtr<FRDGPooledBuffer>&  GetShadingBinDataBufferRef() { return ShadingBinDataBuffer; }
	TRefCountPtr<IPooledRenderTarget>& GetFastClearTileVisRef() { return FastClearTileVis; }

#if !UE_BUILD_SHIPPING
	FFeedbackManager* GetFeedbackManager() { return FeedbackManager; }
#endif
private:
	PassBuffers MainPassBuffers;
	PassBuffers PostPassBuffers;

	// Used for statistics
	TRefCountPtr<FRDGPooledBuffer> StatsBuffer;

	// Used for visualizations
	TRefCountPtr<FRDGPooledBuffer> ShadingBinDataBuffer;
	TRefCountPtr<IPooledRenderTarget> FastClearTileVis;

#if !UE_BUILD_SHIPPING
	FFeedbackManager* FeedbackManager = nullptr;
#endif
};

extern TGlobalResource< FGlobalResources > GGlobalResources;

} // namespace Nanite

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteShadingUniformParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,		ClusterPageData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,		VisibleClustersSWHW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,		HierarchyBuffer)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,			ShadingMask)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>,		VisBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>,		DbgBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,			DbgBuffer32)

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,		ShadingBinData)

	// Multi view
	SHADER_PARAMETER(uint32,												MultiViewEnabled)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,					MultiViewIndices)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>,				MultiViewRectScaleOffsets)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedNaniteView>,	InViews)
END_SHADER_PARAMETER_STRUCT()

extern TRDGUniformBufferRef<FNaniteShadingUniformParameters> CreateDebugNaniteShadingUniformBuffer(FRDGBuilder& GraphBuilder);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteRasterUniformParameters, )
	SHADER_PARAMETER(FIntVector4,	PageConstants)
	SHADER_PARAMETER(uint32,		MaxNodes)
	SHADER_PARAMETER(uint32,		MaxVisibleClusters)
	SHADER_PARAMETER(uint32,		MaxCandidatePatches)
	SHADER_PARAMETER(uint32,		MaxPatchesPerGroup)
	SHADER_PARAMETER(uint32,		MeshPass)
	SHADER_PARAMETER(float,			InvDiceRate)
	SHADER_PARAMETER(uint32,		RenderFlags)
	SHADER_PARAMETER(uint32,		DebugFlags)
END_SHADER_PARAMETER_STRUCT()

extern TRDGUniformBufferRef<FNaniteRasterUniformParameters> CreateDebugNaniteRasterUniformBuffer(FRDGBuilder& GraphBuilder);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNaniteRayTracingUniformParameters, RENDERER_API)
	SHADER_PARAMETER(FIntVector4,	PageConstants)
	SHADER_PARAMETER(uint32,		MaxNodes)

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, RayTracingDataBuffer)
END_SHADER_PARAMETER_STRUCT()

class FNaniteGlobalShader : public FGlobalShader
{
public:
	FNaniteGlobalShader() = default;
	FNaniteGlobalShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FGlobalShader(Initializer)
	{
	}
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);

		// Use the spline mesh texture when possible for performance
		OutEnvironment.SetDefine(TEXT("USE_SPLINE_MESH_SCENE_RESOURCES"), UseSplineMeshSceneResources(Parameters.Platform));

		// Force shader model 6.0+
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
		OutEnvironment.CompilerFlags.Add(CFLAG_WarningsAsErrors);
	}
};

class FNaniteMaterialShader : public FMaterialShader
{
public:
	FNaniteMaterialShader() = default;
	FNaniteMaterialShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	: FMaterialShader(Initializer)
	{
	}

	static bool IsVertexProgrammable(const FMaterialShaderParameters& MaterialParameters, bool bHWRasterShader)
	{
		const bool bPixelProgrammable = IsPixelProgrammable(MaterialParameters);
		const bool bHasVertexUVs = bPixelProgrammable && (MaterialParameters.bHasVertexInterpolator || MaterialParameters.NumCustomizedUVs > 0);
		const bool bHasTessellation = (!bHWRasterShader && MaterialParameters.bIsTessellationEnabled);
		return MaterialParameters.bHasVertexPositionOffsetConnected || bHasVertexUVs || bHasTessellation || MaterialParameters.bSupportsMaterialCache;
	}

	static bool IsVertexProgrammable(uint32 MaterialBitFlags)
	{
		return (MaterialBitFlags & NANITE_MATERIAL_VERTEX_PROGRAMMABLE_FLAGS);
	}

	static bool IsPixelProgrammable(const FMaterialShaderParameters& MaterialParameters)
	{
		return MaterialParameters.bIsMasked || MaterialParameters.bHasPixelDepthOffsetConnected;
	}

	static bool IsPixelProgrammable(uint32 MaterialBitFlags)
	{
		return (MaterialBitFlags & NANITE_MATERIAL_PIXEL_PROGRAMMABLE_FLAGS);
	}

	static bool ShouldCompileProgrammablePermutation(
		const FMaterialShaderParameters& MaterialParameters,
		bool bPermutationVertexProgrammable,
		bool bPermutationPixelProgrammable,
		bool bHWRasterShader)
	{
		if (MaterialParameters.bIsDefaultMaterial)
		{
			return true;
		}

		// Custom materials should compile only the specific combination that is actually used
		// TODO: The status of material attributes on the FMaterialShaderParameters is determined without knowledge of any static
		// switches' values, and therefore when true could represent the set of materials that both enable them and do not. We could
		// isolate a narrower set of required shaders if FMaterialShaderParameters reflected the status after static switches are
		// applied.
		//return IsVertexProgrammable(MaterialParameters, bHWRasterShader) == bPermutationVertexProgrammable &&	
		//		IsPixelProgrammable(MaterialParameters) == bPermutationPixelProgrammable;
		return	(IsVertexProgrammable(MaterialParameters, bHWRasterShader) || !bPermutationVertexProgrammable) &&
				(IsPixelProgrammable(MaterialParameters) || !bPermutationPixelProgrammable) &&
				(bPermutationVertexProgrammable || bPermutationPixelProgrammable);
	}


	static bool ShouldCompilePixelPermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		// Always compile default material as the fast opaque "fixed function" raster path
		bool bValidMaterial = Parameters.MaterialParameters.bIsDefaultMaterial;

		// Compile this pixel shader if it requires programmable raster
		if (Parameters.MaterialParameters.bIsUsedWithNanite && FNaniteMaterialShader::IsPixelProgrammable(Parameters.MaterialParameters))
		{
			bValidMaterial = true;
		}

		return
			DoesPlatformSupportNanite(Parameters.Platform) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
			bValidMaterial;
	}
	
	static bool ShouldCompileVertexPermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		// Always compile default material as the fast opaque "fixed function" raster path
		bool bValidMaterial = Parameters.MaterialParameters.bIsDefaultMaterial;

		// Compile this vertex shader if it requires programmable raster
		static const bool bHWRasterShader = true; // all vertex permutations are HWRaster
		if (Parameters.MaterialParameters.bIsUsedWithNanite &&
			FNaniteMaterialShader::IsVertexProgrammable(Parameters.MaterialParameters, bHWRasterShader))
		{
			bValidMaterial = true;
		}

		return
			DoesPlatformSupportNanite(Parameters.Platform) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
			bValidMaterial;
	}
	
	static bool ShouldCompileComputePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		// Always compile default material as the fast opaque "fixed function" raster path
		bool bValidMaterial = Parameters.MaterialParameters.bIsDefaultMaterial;

		// Compile this compute shader if it requires programmable raster
		static const bool bHWRasterShader = false; // all compute permutations are SWRaster
		if (Parameters.MaterialParameters.bIsUsedWithNanite &&
			(IsVertexProgrammable(Parameters.MaterialParameters, bHWRasterShader) || IsPixelProgrammable(Parameters.MaterialParameters)))
		{
			bValidMaterial = true;
		}

		return
			DoesPlatformSupportNanite(Parameters.Platform) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Surface &&
			bValidMaterial;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Force shader model 6.0+
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
		OutEnvironment.CompilerFlags.Add(CFLAG_HLSL2021);
		OutEnvironment.CompilerFlags.Add(CFLAG_ShaderBundle);
		OutEnvironment.CompilerFlags.Add(CFLAG_RootConstants);

		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_MATERIAL_SHADER"), 1);

		OutEnvironment.SetDefine(TEXT("IS_NANITE_RASTER_PASS"), 1);
		OutEnvironment.SetDefine(TEXT("IS_NANITE_PASS"), 1);

		OutEnvironment.SetDefine(TEXT("NANITE_USE_SHADING_UNIFORM_BUFFER"), 0);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_RASTER_UNIFORM_BUFFER"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_VIEW_UNIFORM_BUFFER"), 0);

		// Force definitions of GetObjectWorldPosition(), etc..
		OutEnvironment.SetDefine(TEXT("HAS_PRIMITIVE_UNIFORM_BUFFER"), 1);

		OutEnvironment.SetDefine(TEXT("ALWAYS_EVALUATE_WORLD_POSITION_OFFSET"),
			Parameters.MaterialParameters.bAlwaysEvaluateWorldPositionOffset ? 1 : 0);
		
		// Use the spline mesh texture when possible for performance
		OutEnvironment.SetDefine(TEXT("USE_SPLINE_MESH_SCENE_RESOURCES"), UseSplineMeshSceneResources(Parameters.Platform));
	}
};

class FMaterialRenderProxy;

class FHWRasterizePS;
class FHWRasterizeVS;
class FHWRasterizeMS;
class FMicropolyRasterizeCS;

struct FNaniteRasterPipeline
{
	const FMaterialRenderProxy* RasterMaterial = nullptr;

	FDisplacementScaling DisplacementScaling;
	FDisplacementFadeRange DisplacementFadeRange;

	bool bIsTwoSided : 1 = false;
	bool bWPOEnabled : 1 = false;
	bool bDisplacementEnabled : 1 = false;
	bool bPerPixelEval : 1 = false;
	bool bSplineMesh : 1 = false;
	bool bSkinnedMesh : 1 = false;
	bool bVoxel : 1 = false;
	bool bHasWPODistance : 1 = false;
	bool bHasPixelDistance : 1 = false;
	bool bHasDisplacementFadeOut : 1 = false;
	bool bFixedDisplacementFallback : 1 = false;
	bool bCastShadow : 1 = false;
	bool bVertexUVs : 1 = false;

	static FNaniteRasterPipeline GetFixedFunctionPipeline(uint8 BinMask);

	uint32 GetPipelineHash() const;
	bool GetFallbackPipeline(FNaniteRasterPipeline& OutFallback) const;

	FORCENOINLINE friend uint32 GetTypeHash(const FNaniteRasterPipeline& Other)
	{
		return Other.GetPipelineHash();
	}
};

struct FNaniteRasterBin
{
	int32  BinId = INDEX_NONE;
	uint16 BinIndex = 0xFFFFu;

	inline bool operator==(const FNaniteRasterBin& Other) const
	{
		return Other.BinId == BinId && Other.BinIndex == BinIndex;
	}
	
	inline bool operator!=(const FNaniteRasterBin& Other) const
	{
		return !(*this == Other);
	}

	inline bool IsValid() const
	{
		return *this != FNaniteRasterBin();
	}
};

struct FNaniteRasterMaterialCacheKey
{
	union
	{
		struct
		{
			uint32 FeatureLevel					: 3;
			uint32 bWPOEnabled					: 1;
			uint32 bPerPixelEval				: 1;
			uint32 bUseMeshShader				: 1;
			uint32 bUsePrimitiveShader			: 1;
			uint32 bDisplacementEnabled			: 1;
			uint32 bVisualizeActive				: 1;
			uint32 bHasVirtualShadowMap			: 1;
			uint32 bIsDepthOnly					: 1;
			uint32 bIsTwoSided					: 1;
			uint32 bCastShadow					: 1;
			uint32 bVoxel						: 1;
			uint32 bSplineMesh					: 1;
			uint32 bSkinnedMesh					: 1;
			uint32 bFixedDisplacementFallback	: 1;
			uint32 bUseWorkGraphSW				: 1;
			uint32 bUseWorkGraphHW				: 1;
			uint32 Unused						: 13;
		};

		uint32 Packed = 0;
	};

	bool operator < (FNaniteRasterMaterialCacheKey Other) const
	{
		return Packed < Other.Packed;
	}

	bool operator == (FNaniteRasterMaterialCacheKey Other) const
	{
		return Packed == Other.Packed;
	}

	bool operator != (FNaniteRasterMaterialCacheKey Other) const
	{
		return Packed != Other.Packed;
	}
};

static_assert((int32)ERHIFeatureLevel::Num <= 8);
static_assert(sizeof(FNaniteRasterMaterialCacheKey) == sizeof(uint32));

inline uint32 GetTypeHash(const FNaniteRasterMaterialCacheKey& Key)
{
	return Key.Packed;
}

struct FNaniteRasterMaterialCache
{
	const FMaterial* VertexMaterial = nullptr;
	const FMaterial* PixelMaterial = nullptr;
	const FMaterial* ComputeMaterial = nullptr;
	const FMaterialRenderProxy* VertexMaterialProxy = nullptr;
	const FMaterialRenderProxy* PixelMaterialProxy = nullptr;
	const FMaterialRenderProxy* ComputeMaterialProxy = nullptr;

	TShaderRef<FHWRasterizePS> RasterPixelShader;
	TShaderRef<FHWRasterizeVS> RasterVertexShader;
	TShaderRef<FHWRasterizeMS> RasterMeshShader;
	TShaderRef<FMicropolyRasterizeCS> ClusterComputeShader;
	TShaderRef<FMicropolyRasterizeCS> PatchComputeShader;

	TOptional<uint32> MaterialBitFlags;
	TOptional<FDisplacementScaling> DisplacementScaling;
	TOptional<FDisplacementFadeRange> DisplacementFadeRange;

	bool bFinalized = false;
};

struct FNaniteRasterEntry
{
	mutable TMap<FNaniteRasterMaterialCacheKey, FNaniteRasterMaterialCache> CacheMap;

	FNaniteRasterPipeline RasterPipeline{};
	uint32 ReferenceCount = 0;
	uint16 BinIndex = 0xFFFFu;
};

struct FNaniteRasterEntryKeyFuncs : TDefaultMapHashableKeyFuncs<FNaniteRasterPipeline, FNaniteRasterEntry, false>
{
	static inline bool Matches(KeyInitType A, KeyInitType B)
	{
		return A.GetPipelineHash() == B.GetPipelineHash() && A.RasterMaterial == B.RasterMaterial;
	}

	static inline uint32 GetKeyHash(KeyInitType Key)
	{
		return Key.GetPipelineHash();
	}
};

using FNaniteRasterPipelineMap = Experimental::TRobinHoodHashMap<FNaniteRasterPipeline, FNaniteRasterEntry, FNaniteRasterEntryKeyFuncs>;

class FNaniteRasterBinIndexTranslator
{
public:
	FNaniteRasterBinIndexTranslator() = default;

	uint16 Translate(uint16 BinIndex) const
	{
		return BinIndex < RegularBinCount ? BinIndex : RevertBinIndex(BinIndex) + RegularBinCount;
	}

private:
	friend class FNaniteRasterPipelines;

	uint32 RegularBinCount;

	FNaniteRasterBinIndexTranslator(uint32 InRegularBinCount)
		: RegularBinCount(InRegularBinCount)
	{}

	static uint16 RevertBinIndex(uint16 BinIndex)
	{
		return MAX_uint16 - BinIndex;
	}
};

class FNaniteRasterPipelines
{
public:
	typedef Experimental::FHashType FRasterHash;
	typedef Experimental::FHashElementId FRasterId;

public:
	FNaniteRasterPipelines();
	~FNaniteRasterPipelines();

	void AllocateFixedFunctionBins();
	void ReleaseFixedFunctionBins();
	void ReloadFixedFunctionBins();

	uint16 AllocateBin(bool bPerPixelEval);
	void ReleaseBin(uint16 BinIndex);

	bool IsBinAllocated(uint16 BinIndex) const;

	uint32 GetRegularBinCount() const;
	uint32 GetBinCount() const;

	FNaniteRasterBin Register(const FNaniteRasterPipeline& InRasterPipeline);
	void Unregister(const FNaniteRasterBin& InRasterBin);

	inline const FNaniteRasterPipelineMap& GetRasterPipelineMap() const
	{
		return PipelineMap;
	}

	inline FNaniteRasterBinIndexTranslator GetBinIndexTranslator() const
	{
		return FNaniteRasterBinIndexTranslator(GetRegularBinCount());
	}

	/**
	 * These "Custom Pass" methods allow for a rasterization pass that renders a subset of the objects in the mesh pass that
	 * registered these pipelines, and aims to exclude rasterizing unused bins for performance (e.g. Custom Depth pass).
	 **/
	void RegisterBinForCustomPass(uint16 BinIndex);
	void UnregisterBinForCustomPass(uint16 BinIndex);
	bool ShouldBinRenderInCustomPass(uint16 BinIndex) const;

private:
	TBitArray<> PipelineBins;
	TBitArray<> PerPixelEvalPipelineBins;
	TArray<uint32> CustomPassRefCounts;
	TArray<uint32> PerPixelEvalCustomPassRefCounts;
	FNaniteRasterPipelineMap PipelineMap;

	struct FFixedFunctionBin
	{
		FNaniteRasterBin RasterBin;
		uint8 BinMask;
	};

	TArray<FFixedFunctionBin, TInlineAllocator<6u>> FixedFunctionBins;
};

struct FNaniteShadingBin
{
	int32  BinId = INDEX_NONE;
	uint16 BinIndex = 0xFFFFu;

	inline bool operator==(const FNaniteShadingBin& Other) const
	{
		return Other.BinId == BinId && Other.BinIndex == BinIndex;
	}

	inline bool operator!=(const FNaniteShadingBin& Other) const
	{
		return !(*this == Other);
	}

	inline bool IsValid() const
	{
		return *this != FNaniteShadingBin();
	}
};

struct FNaniteBasePassData;
struct FNaniteLumenCardData;
struct FNaniteMaterialCacheData;
class FMeshDrawShaderBindings;

struct FNaniteShadingPipeline
{
	TPimplPtr<FNaniteBasePassData, EPimplPtrMode::DeepCopy> BasePassData;
	TPimplPtr<FNaniteLumenCardData, EPimplPtrMode::DeepCopy> LumenCardData;
	TPimplPtr<FNaniteMaterialCacheData, EPimplPtrMode::DeepCopy> MaterialCacheData;
	TPimplPtr<FMeshDrawShaderBindings, EPimplPtrMode::DeepCopy> ShaderBindings;

	const FMaterialRenderProxy* MaterialProxy = nullptr;
	const FMaterial* Material = nullptr;
	FRHIComputeShader* ComputeShader = nullptr;
	FRHIWorkGraphShader* WorkGraphShader = nullptr;

#if WITH_DEBUG_VIEW_MODES
	uint32 InstructionCount = 0;
	uint32 LWCComplexity = 0;
#endif

	uint32 BoundTargetMask = 0u;
	uint32 ShaderBindingsHash = 0u;
	uint32 MaterialBitFlags = 0x0u;

	// Shading flags
	union
	{
		struct
		{
			uint16 bIsTwoSided : 1;
			uint16 bIsMasked : 1;
			uint16 bNoDerivativeOps : 1;
			uint16 bPadding : 13;
		};

		uint16 ShadingFlagsHash = 0;
	};

	inline uint32 GetPipelineHash() const
	{
		// Ignoring the lower 4 bits since they are likely zero anyway.
		// Higher bits are more significant in 64 bit builds.
		uint64 PipelineHash = uint64(reinterpret_cast<UPTRINT>(MaterialProxy) >> 4);

		// Combine shader flags hash and material hash
		PipelineHash = CityHash128to64({ PipelineHash, ShadingFlagsHash });

		// Combine with bound target mask
		PipelineHash = CityHash128to64({ PipelineHash, BoundTargetMask });

		// Combine with shader bindings hash
		PipelineHash = CityHash128to64({ PipelineHash, ShaderBindingsHash });

		return HashCombineFast(uint32(PipelineHash & 0xFFFFFFFF), uint32((PipelineHash >> 32) & 0xFFFFFFFF));
	}

	FORCENOINLINE friend uint32 GetTypeHash(const FNaniteShadingPipeline& Other)
	{
		return Other.GetPipelineHash();
	}
};

struct FNaniteShadingEntry
{
	TSharedPtr<FNaniteShadingPipeline> ShadingPipeline;
	uint32 ReferenceCount = 0;
	uint16 BinIndex = 0xFFFFu;
};

struct FNaniteShadingEntryKeyFuncs : TDefaultMapHashableKeyFuncs<FNaniteShadingPipeline, FNaniteShadingEntry, false>
{
	static inline bool Matches(KeyInitType A, KeyInitType B)
	{
		return A.GetPipelineHash() == B.GetPipelineHash() && A.MaterialProxy == B.MaterialProxy;
	}

	static inline uint32 GetKeyHash(KeyInitType Key)
	{
		return Key.GetPipelineHash();
	}
};

using FNaniteShadingPipelineMap = Experimental::TRobinHoodHashMap<FNaniteShadingPipeline, FNaniteShadingEntry, FNaniteShadingEntryKeyFuncs>;

class FNaniteShadingPipelines
{
public:
	typedef Experimental::FHashType FShadingHash;
	typedef Experimental::FHashElementId FShadingId;

public:
	FNaniteShadingPipelines();
	~FNaniteShadingPipelines();

	uint16 AllocateBin();
	void ReleaseBin(uint16 BinIndex);

	bool IsBinAllocated(uint16 BinIndex) const;

	uint32 GetBinCount() const;

	FNaniteShadingBin Register(const FNaniteShadingPipeline& InShadingPipeline);
	void Unregister(const FNaniteShadingBin& InShadingBin);

	inline const FNaniteShadingPipelineMap& GetShadingPipelineMap() const
	{
		return PipelineMap;
	}

	bool bBuildCommands = true;

	FPrimitiveViewRelevance CombinedRelevance;

	void BuildIdList();
	const TConstArrayView<const FShadingId> GetIdList() const;

	void ComputeRelevance(ERHIFeatureLevel::Type InFeatureLevel);

private:
	TBitArray<> PipelineBins;
	FNaniteShadingPipelineMap PipelineMap;
	TArray<FShadingId> ShadingIdList;
	bool bBuildIdList = true;
};

struct FNaniteShadingCommand
{
	TSharedPtr<FNaniteShadingPipeline> Pipeline;
	FUint32Vector4 PassData;
	uint16 ShadingBin = 0xFFFFu;
	bool bVisible = true;

	// The PSO precache state - updated at dispatch time and can be used to skip command when still precaching
	EPSOPrecacheResult PSOPrecacheState = EPSOPrecacheResult::Unknown;
};

struct FNaniteShadingCommands
{
	using FMetaBufferArray = TArray<FUintVector4, SceneRenderingAllocator>;

	uint32 MaxShadingBin = 0u;
	uint32 NumCommands = 0u;
	uint32 BoundTargetMask = 0x0u;
	FShaderBundleRHIRef ShaderBundle;
	TArray<FNaniteShadingCommand> Commands;
	TArray<int32> CommandLookup;
	FMetaBufferArray MetaBufferData;

	UE::Tasks::FTask SetupTask;
	UE::Tasks::FTask BuildCommandsTask;
};

extern bool ShouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport = true);

/** Checks whether Nanite would be rendered in this view. Used to give a visual warning about the project settings that can disable Nanite. */
extern bool WouldRenderNanite(const FScene* Scene, const FViewInfo& View, bool bCheckForAtomicSupport = true, bool bCheckForProjectSetting = true);

extern bool UseComputeDepthExport();

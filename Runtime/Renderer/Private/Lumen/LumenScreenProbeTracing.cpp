// Copyright Epic Games, Inc. All Rights Reserved.
	
#include "LumenScreenProbeGather.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "HairStrands/HairStrandsData.h"

int32 GLumenScreenProbeGatherScreenTraces = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherScreenTraces(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"),
	GLumenScreenProbeGatherScreenTraces,
	TEXT("Whether to trace against the screen before falling back to other tracing methods."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenScreenProbeGatherScreenTracesHairBias(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HairBias"),
	2.0f,
	TEXT("Bias for screen space rays traced from hair pixels. Usually hair has pretty complex geometry and requires a special bias value."),
	ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHierarchicalScreenTraces = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHierarchicalScreenTraces(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal"),
	GLumenScreenProbeGatherHierarchicalScreenTraces,
	TEXT("Whether to use HZB tracing for SSGI instead of fixed step count intersection.  HZB tracing is much more accurate, in particular not missing thin features, but is about ~3x slower."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHierarchicalScreenTracesFullResDepth = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHierarchicalScreenTracesFullResDepth(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.FullResDepth"),
	GLumenScreenProbeGatherHierarchicalScreenTracesFullResDepth,
	TEXT("Whether the HZB traversal should go all the way down to the full resolution depth, which is more accurate but adds incoherency to the inner loop."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHierarchicalScreenTracesSkipFoliageHits = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHierarchicalScreenTracesSkipFoliageHits( 
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.SkipFoliageHits"),
	GLumenScreenProbeGatherHierarchicalScreenTracesSkipFoliageHits,
	TEXT("Whether to allow screen traces to hit Subsurface and TwoSided Foliage shading models.  Can be used to work around aliasing from high frequency grass geometry."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> GVarLumenScreenProbeGatherHierarchicalScreenTracesSkipHairHits(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.SkipHairHits"),
	0,
	TEXT("Whether to allow screen traces to hit hair shading models.  Can be used to work around aliasing from high frequency hair cards geometry."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations = 50;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.MaxIterations"),
	GLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations,
	TEXT("Max iterations for HZB tracing."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherRelativeDepthThickness = .02f;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherRelativeDepthThickness(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.RelativeDepthThickness"),
	GLumenScreenProbeGatherRelativeDepthThickness,
	TEXT("Determines depth thickness of objects hit by HZB tracing, as a relative depth threshold."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherHistoryDepthTestRelativeThickness = 0.005f;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHistoryDepthTestRelativeThickness(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.HistoryDepthTestRelativeThickness"),
	GLumenScreenProbeGatherHistoryDepthTestRelativeThickness,
	TEXT("Distance between HZB trace hit and previous frame scene depth from which to allow hits, as a relative depth threshold."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty = 4;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.NumThicknessStepsToDetermineCertainty"),
	GLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty,
	TEXT("Number of linear search steps to determine if a hit feature is thin and should be ignored."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherVisualizeTraces = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherVisualizeTraces(
	TEXT("r.Lumen.ScreenProbeGather.VisualizeTraces"),
	GLumenScreenProbeGatherVisualizeTraces,
	TEXT("Whether to visualize traces for the center screen probe, useful for debugging"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherVisualizeTracesFreeze = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherVisualizeTracesFreeze(
	TEXT("r.Lumen.ScreenProbeGather.VisualizeTracesFreeze"),
	GLumenScreenProbeGatherVisualizeTracesFreeze,
	TEXT("Whether to freeze updating the visualize trace data.  Note that no changes to cvars or shaders will propagate until unfrozen."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHairStrands_VoxelTrace = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHairStrands_VoxelTrace(
	TEXT("r.Lumen.ScreenProbeGather.HairStrands.VoxelTrace"),
	GLumenScreenProbeGatherHairStrands_VoxelTrace,
	TEXT("Whether to trace against hair voxel structure for hair casting shadow onto opaques."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHairStrands_ScreenTrace = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHairStrands_ScreenTrace(
	TEXT("r.Lumen.ScreenProbeGather.HairStrands.ScreenTrace"),
	GLumenScreenProbeGatherHairStrands_ScreenTrace,
	TEXT("Whether to trace against hair depth for hair casting shadow onto opaques."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherScreenTracesMinimumOccupancy = 0;
FAutoConsoleVariableRef CVarLumenScreenProbeGatherScreenTraceMinimumOccupancy(
	TEXT("r.Lumen.ScreenProbeGather.ScreenTraces.MinimumOccupancy"),
	GLumenScreenProbeGatherScreenTracesMinimumOccupancy,
	TEXT("Minimum number of threads still tracing before aborting the trace.  Can be used for scalability to abandon traces that have a disproportionate cost."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

bool SupportsHairScreenTraces()
{
	return GVarLumenScreenProbeGatherHierarchicalScreenTracesSkipHairHits.GetValueOnRenderThread() == 0;
}

class FClearTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FClearTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearTracesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ClearTracesCS", SF_Compute);

class FScreenProbeTraceScreenTexturesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTraceScreenTexturesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTraceScreenTexturesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHZBScreenTraceParameters, HZBScreenTraceParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSubstrateGlobalUniformParameters, Substrate)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, LightingChannelsTexture)
		SHADER_PARAMETER(uint32, ScreenProbeGatherStateFrameIndex)
		SHADER_PARAMETER(float, MaxHierarchicalScreenTraceIterations)
		SHADER_PARAMETER(float, RelativeDepthThickness)
		SHADER_PARAMETER(float, HistoryDepthTestRelativeThickness)
		SHADER_PARAMETER(float, NumThicknessStepsToDetermineCertainty)
		SHADER_PARAMETER(float, BiasForTracesFromHairPixels)
		SHADER_PARAMETER(uint32, MinimumTracingThreadOccupancy)
		SHADER_PARAMETER(uint32, SkipFoliageHits)
		SHADER_PARAMETER(uint32, SkipHairHits)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
	END_SHADER_PARAMETER_STRUCT()

	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	class FHierarchicalScreenTracing : SHADER_PERMUTATION_BOOL("HIERARCHICAL_SCREEN_TRACING");
	class FTraceFullResDepth : SHADER_PERMUTATION_BOOL("HZB_TRACE_INCLUDE_FULL_RES_DEPTH");
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	class FHairStrands : SHADER_PERMUTATION_BOOL("USE_HAIRSTRANDS_SCREEN");
	class FTerminateOnLowOccupancy : SHADER_PERMUTATION_BOOL("TERMINATE_ON_LOW_OCCUPANCY");
	class FUseDistanceFieldRepresentationBit : SHADER_PERMUTATION_BOOL("USE_DISTANCE_FIELD_REPRESENTATION_BIT");

	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling, FHierarchicalScreenTracing, FTraceFullResDepth, FRadianceCache, FHairStrands, FTerminateOnLowOccupancy, FUseDistanceFieldRepresentationBit>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FTerminateOnLowOccupancy>() && !RHISupportsWaveOperations(Parameters.Platform))
		{
			return false;
		}

		if (PermutationVector.Get<FTraceFullResDepth>() && !PermutationVector.Get<FHierarchicalScreenTracing>())
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		const bool bTerminateOnLowOccupancy = GLumenScreenProbeGatherScreenTracesMinimumOccupancy > 0
			&& GRHISupportsWaveOperations
			&& GRHIMinimumWaveSize <= 32
			&& GRHIMaximumWaveSize >= 32
			&& RHISupportsWaveOperations(Parameters.Platform);
		const bool bHZBTraversal = GLumenScreenProbeGatherHierarchicalScreenTraces != 0;

		if (PermutationVector.Get<FRadianceCache>() != LumenScreenProbeGather::UseRadianceCache())
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}
		if (PermutationVector.Get<FHierarchicalScreenTracing>() != bHZBTraversal)
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}
		if (PermutationVector.Get<FTraceFullResDepth>() != (bHZBTraversal && GLumenScreenProbeGatherHierarchicalScreenTracesFullResDepth != 0))
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		return EShaderPermutationPrecacheRequest::Precached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FTerminateOnLowOccupancy>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTraceScreenTexturesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeTraceScreenTexturesCS", SF_Compute);

class FScreenProbeCompactTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeCompactTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeCompactTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER(uint32, CullByDistanceFromCamera)
		SHADER_PARAMETER(float, CompactionTracingEndDistanceFromCamera)
		SHADER_PARAMETER(float, CompactionMaxTraceDistance)
		SHADER_PARAMETER(uint32, CompactForSkyApply)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactedTraceTexelData)
	END_SHADER_PARAMETER_STRUCT()

	static int32 GetGroupSize()
	{
		return 16;
	}

	class FWaveOps : SHADER_PERMUTATION_BOOL("WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>() && !RHISupportsWaveOperations(Parameters.Platform))
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeCompactTracesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeCompactTracesCS", SF_Compute);

class FSetupCompactedTracesIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupCompactedTracesIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupCompactedTracesIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWScreenProbeCompactTracingIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupCompactedTracesIndirectArgsCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "SetupCompactedTracesIndirectArgsCS", SF_Compute);

class FScreenProbeTraceMeshSDFsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTraceMeshSDFsCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTraceMeshSDFsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, HairStrandsVoxel)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)
	END_SHADER_PARAMETER_STRUCT()
		
	class FThreadGroupSize32 : SHADER_PERMUTATION_BOOL("THREADGROUP_SIZE_32");
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	class FHairStrands : SHADER_PERMUTATION_BOOL("USE_HAIRSTRANDS_VOXEL");
	class FTraceMeshSDFs : SHADER_PERMUTATION_BOOL("SCENE_TRACE_MESH_SDFS");
	class FTraceHeightfields : SHADER_PERMUTATION_BOOL("SCENE_TRACE_HEIGHTFIELDS");
	class FOffsetDataStructure : SHADER_PERMUTATION_INT("OFFSET_DATA_STRUCT", 3);
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize32, FStructuredImportanceSampling, FHairStrands, FTraceMeshSDFs, FTraceHeightfields, FOffsetDataStructure>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		// FOffsetDataStructure is only used for mesh SDFs
		if (!PermutationVector.Get<FTraceMeshSDFs>())
		{
			PermutationVector.Set<FOffsetDataStructure>(0);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector = RemapPermutation(FPermutationDomain(Parameters.PermutationId));

		extern int32 GDistanceFieldOffsetDataStructure;
		if (PermutationVector.Get<FOffsetDataStructure>() != GDistanceFieldOffsetDataStructure)
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		if (PermutationVector.Get<FThreadGroupSize32>() != Lumen::UseThreadGroupSize32())
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		return EShaderPermutationPrecacheRequest::Precached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTraceMeshSDFsCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeTraceMeshSDFsCS", SF_Compute);


class FScreenProbeTraceVoxelsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTraceVoxelsCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTraceVoxelsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheInterpolationParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, HairStrandsVoxel)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FThreadGroupSize32 : SHADER_PERMUTATION_BOOL("THREADGROUP_SIZE_32");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	class FScreenProbeExtraAO : SHADER_PERMUTATION_BOOL("SCREEN_PROBE_EXTRA_AO");
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	class FHairStrands : SHADER_PERMUTATION_BOOL("USE_HAIRSTRANDS_VOXEL");
	class FTraceVoxels : SHADER_PERMUTATION_BOOL("TRACE_VOXELS");
	class FSimpleCoverageBasedExpand : SHADER_PERMUTATION_BOOL("GLOBALSDF_SIMPLE_COVERAGE_BASED_EXPAND");
	using FPermutationDomain = TShaderPermutationDomain<FThreadGroupSize32, FRadianceCache, FScreenProbeExtraAO, FStructuredImportanceSampling, FHairStrands, FTraceVoxels, FSimpleCoverageBasedExpand>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (!PermutationVector.Get<FTraceVoxels>())
		{
			PermutationVector.Set<FSimpleCoverageBasedExpand>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static EShaderPermutationPrecacheRequest ShouldPrecachePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FThreadGroupSize32>() != Lumen::UseThreadGroupSize32())
		{
			return EShaderPermutationPrecacheRequest::NotUsed;
		}

		return EShaderPermutationPrecacheRequest::Precached;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTraceVoxelsCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeTraceVoxelsCS", SF_Compute);



class FScreenProbeSetupVisualizeTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeSetupVisualizeTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeSetupVisualizeTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWVisualizeTracesData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
	END_SHADER_PARAMETER_STRUCT()
		
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize() 
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeSetupVisualizeTracesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeSetupVisualizeTraces", SF_Compute);

TRefCountPtr<FRDGPooledBuffer> GVisualizeTracesData;

void SetupVisualizeTraces(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FScene* Scene,
	const FViewInfo& View,
	const FScreenProbeParameters& ScreenProbeParameters,
	ERDGPassFlags ComputePassFlags)
{
	FRDGBufferRef VisualizeTracesData = nullptr;

	if (GVisualizeTracesData.IsValid())
	{
		VisualizeTracesData = GraphBuilder.RegisterExternalBuffer(GVisualizeTracesData);
	}
	
	const int32 VisualizeBufferNumElements = ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * 3;
	bool bShouldUpdate = !GLumenScreenProbeGatherVisualizeTracesFreeze;

	if (!VisualizeTracesData || VisualizeTracesData->Desc.NumElements != VisualizeBufferNumElements)
	{
		VisualizeTracesData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), VisualizeBufferNumElements), TEXT("VisualizeTracesData"));
		bShouldUpdate = true;
	}

	if (bShouldUpdate)
	{
		FScreenProbeSetupVisualizeTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeSetupVisualizeTracesCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->RWVisualizeTracesData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(VisualizeTracesData, PF_A32B32G32R32F));
		PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;

		FScreenProbeSetupVisualizeTracesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeSetupVisualizeTracesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling(View));
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeSetupVisualizeTracesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupVisualizeTraces"),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(ScreenProbeParameters.ScreenProbeTracingOctahedronResolution), FScreenProbeSetupVisualizeTracesCS::GetGroupSize()));

		GVisualizeTracesData = GraphBuilder.ConvertToExternalBuffer(VisualizeTracesData);
	}
}

void GetScreenProbeVisualizeTracesBuffer(TRefCountPtr<FRDGPooledBuffer>& VisualizeTracesData)
{
	if (GLumenScreenProbeGatherVisualizeTraces && GVisualizeTracesData.IsValid())
	{
		VisualizeTracesData = GVisualizeTracesData;
	}
}

enum class ECompactedTracingIndirectArgs
{
	NumTracesDiv64 = 0 * sizeof(FRHIDispatchIndirectParameters),
	NumTracesDiv32 = 1 * sizeof(FRHIDispatchIndirectParameters),
	NumLightSampleTracesDiv64 = 2 * sizeof(FRHIDispatchIndirectParameters),
	NumLightSampleTracesDiv32 = 3 * sizeof(FRHIDispatchIndirectParameters),
	MAX = 4,
};

FCompactedTraceParameters LumenScreenProbeGather::CompactTraces(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const FScreenProbeParameters& ScreenProbeParameters,
	bool bCullByDistanceFromCamera,
	float CompactionTracingEndDistanceFromCamera,
	float CompactionMaxTraceDistance,
	bool bCompactForSkyApply,
	ERDGPassFlags ComputePassFlags)
{
	FRDGBufferRef CompactedTraceTexelAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("Lumen.ScreenProbeGather.CompactedTraceTexelAllocator"));
	FRDGBufferUAVRef CompactedTraceTexelAllocatorUAV = GraphBuilder.CreateUAV(CompactedTraceTexelAllocator, PF_R32_UINT, ERDGUnorderedAccessViewFlags::SkipBarrier);

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedTraceTexelAllocator, PF_R32_UINT), 0, ComputePassFlags);

	const FIntPoint ScreenProbeTraceBufferSize = ScreenProbeParameters.ScreenProbeAtlasBufferSize * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution;
	const int32 NumCompactedTraceTexelDataElements = ScreenProbeTraceBufferSize.X * ScreenProbeTraceBufferSize.Y;
	FRDGBufferRef CompactedTraceTexelData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCompactedTraceTexelDataElements), TEXT("Lumen.ScreenProbeGather.CompactedTraceTexelData"));

	{
		FScreenProbeCompactTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeCompactTracesCS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->RWCompactedTraceTexelAllocator = CompactedTraceTexelAllocatorUAV;
		PassParameters->RWCompactedTraceTexelData = GraphBuilder.CreateUAV(CompactedTraceTexelData, PF_R32_UINT);
		PassParameters->CullByDistanceFromCamera = bCullByDistanceFromCamera ? 1 : 0;
		PassParameters->CompactionTracingEndDistanceFromCamera = CompactionTracingEndDistanceFromCamera;
		PassParameters->CompactionMaxTraceDistance = CompactionMaxTraceDistance;
		PassParameters->CompactForSkyApply = bCompactForSkyApply ? 1 : 0;

		const bool bWaveOps = Lumen::UseWaveOps(View.GetShaderPlatform())
			&& GRHIMinimumWaveSize <= 32
			&& GRHIMaximumWaveSize >= 32;

		FScreenProbeCompactTracesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FScreenProbeCompactTracesCS::FWaveOps>(bWaveOps);
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeCompactTracesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CompactTraces WaveOps:%d", bWaveOps),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::TraceCompaction * sizeof(FRHIDispatchIndirectParameters));
	}

	FCompactedTraceParameters CompactedTraceParameters;
	CompactedTraceParameters.IndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>((int32)ECompactedTracingIndirectArgs::MAX), TEXT("Lumen.ScreenProbeGather.CompactTracingIndirectArgs"));

	{
		FSetupCompactedTracesIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupCompactedTracesIndirectArgsCS::FParameters>();
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->RWScreenProbeCompactTracingIndirectArgs = GraphBuilder.CreateUAV(CompactedTraceParameters.IndirectArgs, PF_R32_UINT);
		PassParameters->CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceTexelAllocator, PF_R32_UINT));

		auto ComputeShader = View.ShaderMap->GetShader<FSetupCompactedTracesIndirectArgsCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupCompactedTracesIndirectArgs"),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	CompactedTraceParameters.CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceTexelAllocator, PF_R32_UINT));
	CompactedTraceParameters.CompactedTraceTexelData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceTexelData, PF_R32_UINT));

	return CompactedTraceParameters;
}

void TraceScreenProbes(
	FRDGBuilder& GraphBuilder, 
	const FScene* Scene,
	const FViewInfo& View, 
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	bool bTraceMeshObjects,
	const FSceneTextures& SceneTextures,
	FRDGTextureRef LightingChannelsTexture,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FScreenProbeParameters& ScreenProbeParameters,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	ERDGPassFlags ComputePassFlags)
{
	const FSceneTextureParameters& SceneTextureParameters = GetSceneTextureParameters(GraphBuilder, SceneTextures);

	{
		FClearTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearTracesCS::FParameters>();
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FClearTracesCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearTraces %ux%u", ScreenProbeParameters.ScreenProbeTracingOctahedronResolution, ScreenProbeParameters.ScreenProbeTracingOctahedronResolution),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::ThreadPerTrace * sizeof(FRHIDispatchIndirectParameters));
	}

	FLumenCardTracingParameters TracingParameters;
	GetLumenCardTracingParameters(GraphBuilder, View, *Scene->GetLumenSceneData(View), FrameTemporaries, /*bSurfaceCacheFeedback*/ false, TracingParameters);

	FLumenIndirectTracingParameters IndirectTracingParameters;
	SetupLumenDiffuseTracingParameters(View, IndirectTracingParameters);

	extern int32 GLumenVisualizeIndirectDiffuse;
	const bool bTraceScreen = View.PrevViewInfo.ScreenSpaceRayTracingInput.IsValid()
		&& GLumenScreenProbeGatherScreenTraces != 0
		&& GLumenVisualizeIndirectDiffuse == 0
		&& View.Family->EngineShowFlags.LumenScreenTraces
		&& View.FinalPostProcessSettings.LumenFinalGatherScreenTraces;

	if (bTraceScreen)
	{
		auto TraceScreen = [&]()
		{
			FScreenProbeTraceScreenTexturesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTraceScreenTexturesCS::FParameters>();

			PassParameters->HZBScreenTraceParameters = SetupHZBScreenTraceParameters(GraphBuilder, View, SceneTextures);
			PassParameters->TracingParameters = TracingParameters;
			PassParameters->SceneTextures = SceneTextureParameters;

			if (PassParameters->HZBScreenTraceParameters.PrevSceneColorTexture->GetParent() == SceneTextures.Color.Resolve || !PassParameters->SceneTextures.GBufferVelocityTexture)
			{
				PassParameters->SceneTextures.GBufferVelocityTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
			}

			PassParameters->Substrate = Substrate::BindSubstrateGlobalUniformParameters(View);
			PassParameters->LightingChannelsTexture = LightingChannelsTexture;
			PassParameters->MaxHierarchicalScreenTraceIterations = GLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations;
			PassParameters->RelativeDepthThickness = GLumenScreenProbeGatherRelativeDepthThickness * View.ViewMatrices.GetPerProjectionDepthThicknessScale();
			PassParameters->HistoryDepthTestRelativeThickness = GLumenScreenProbeGatherHistoryDepthTestRelativeThickness * View.ViewMatrices.GetPerProjectionDepthThicknessScale();
			PassParameters->NumThicknessStepsToDetermineCertainty = GLumenScreenProbeGatherHierarchicalScreenTracesSkipFoliageHits ? 0 : GLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty;
			PassParameters->MinimumTracingThreadOccupancy = GLumenScreenProbeGatherScreenTracesMinimumOccupancy;
			PassParameters->SkipFoliageHits = GLumenScreenProbeGatherHierarchicalScreenTracesSkipFoliageHits;
			PassParameters->SkipHairHits = !SupportsHairScreenTraces();
			PassParameters->BiasForTracesFromHairPixels = CVarLumenScreenProbeGatherScreenTracesHairBias.GetValueOnRenderThread();
			PassParameters->ScreenProbeGatherStateFrameIndex = LumenScreenProbeGather::GetStateFrameIndex(View.ViewState);

			PassParameters->ScreenProbeParameters = ScreenProbeParameters;
			PassParameters->IndirectTracingParameters = IndirectTracingParameters;
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;

			const bool bHasHairStrands = HairStrands::HasViewHairStrandsData(View) && GLumenScreenProbeGatherHairStrands_ScreenTrace > 0;
			if (bHasHairStrands)
			{
				PassParameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
			}

			const bool bTerminateOnLowOccupancy = GLumenScreenProbeGatherScreenTracesMinimumOccupancy > 0
				&& GRHISupportsWaveOperations
				&& GRHIMinimumWaveSize <= 32
				&& GRHIMaximumWaveSize >= 32
				&& RHISupportsWaveOperations(View.GetShaderPlatform());

			const bool bHZBTraversal = GLumenScreenProbeGatherHierarchicalScreenTraces != 0;

			FScreenProbeTraceScreenTexturesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FRadianceCache >(LumenScreenProbeGather::UseRadianceCache());
			PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FHierarchicalScreenTracing >(bHZBTraversal);
			PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FTraceFullResDepth >(bHZBTraversal && GLumenScreenProbeGatherHierarchicalScreenTracesFullResDepth != 0);
			PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling(View));
			PermutationVector.Set<FScreenProbeTraceScreenTexturesCS::FHairStrands>(bHasHairStrands);
			PermutationVector.Set<FScreenProbeTraceScreenTexturesCS::FTerminateOnLowOccupancy>(bTerminateOnLowOccupancy);
			PermutationVector.Set<FScreenProbeTraceScreenTexturesCS::FUseDistanceFieldRepresentationBit>(Lumen::IsUsingDistanceFieldRepresentationBit(View));

			auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeTraceScreenTexturesCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TraceScreen(%s)", bHasHairStrands ? TEXT("Scene, HairStrands") : TEXT("Scene")),
				ComputePassFlags,
				ComputeShader,
				PassParameters,
				ScreenProbeParameters.ProbeIndirectArgs,
				(uint32)(EScreenProbeIndirectArgs::ThreadPerTrace) * sizeof(FRHIDispatchIndirectParameters));
		};

		TraceScreen();
	}

	bool bNeedTraceHairVoxel = HairStrands::HasViewHairStrandsVoxelData(View) && GLumenScreenProbeGatherHairStrands_VoxelTrace > 0;
	const bool bUseHardwareRayTracing = Lumen::UseHardwareRayTracedScreenProbeGather(*View.Family);

	if (bUseHardwareRayTracing)
	{
		RenderHardwareRayTracingScreenProbe(
			GraphBuilder,
			Scene,
			SceneTextureParameters,
			ScreenProbeParameters,
			View,
			TracingParameters,
			IndirectTracingParameters,
			RadianceCacheParameters,
			ComputePassFlags);
	}
	else if (bTraceMeshObjects)
	{
		CullForCardTracing(
			GraphBuilder,
			Scene, 
			View,
			FrameTemporaries,
			IndirectTracingParameters,
			/* out */ MeshSDFGridParameters,
			ComputePassFlags);

		const bool bTraceMeshSDFs = MeshSDFGridParameters.TracingParameters.DistanceFieldObjectBuffers.NumSceneObjects > 0;
		const bool bTraceHeightfields = Lumen::UseHeightfieldTracing(*View.Family, *Scene->GetLumenSceneData(View));

		if (bTraceMeshSDFs || bTraceHeightfields)
		{
			FCompactedTraceParameters CompactedTraceParameters = LumenScreenProbeGather::CompactTraces(
				GraphBuilder,
				View,
				ScreenProbeParameters,
				true,
				IndirectTracingParameters.CardTraceEndDistanceFromCamera,
				IndirectTracingParameters.MaxMeshSDFTraceDistance,
				/*bCompactForSkyApply*/ false,
				ComputePassFlags);

			auto TraceMeshSDFs = [&]()
			{
				FScreenProbeTraceMeshSDFsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTraceMeshSDFsCS::FParameters>();
				PassParameters->TracingParameters = TracingParameters;
				PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;
				PassParameters->ScreenProbeParameters = ScreenProbeParameters;
				PassParameters->IndirectTracingParameters = IndirectTracingParameters;
				PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
				PassParameters->CompactedTraceParameters = CompactedTraceParameters;
				if (bNeedTraceHairVoxel)
				{
					PassParameters->HairStrandsVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
				}

				FScreenProbeTraceMeshSDFsCS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FScreenProbeTraceMeshSDFsCS::FThreadGroupSize32 >(Lumen::UseThreadGroupSize32());
				PermutationVector.Set< FScreenProbeTraceMeshSDFsCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling(View));
				PermutationVector.Set< FScreenProbeTraceMeshSDFsCS::FHairStrands >(bNeedTraceHairVoxel);
				PermutationVector.Set< FScreenProbeTraceMeshSDFsCS::FTraceMeshSDFs >(bTraceMeshSDFs);
				PermutationVector.Set< FScreenProbeTraceMeshSDFsCS::FTraceHeightfields >(bTraceHeightfields);
				extern int32 GDistanceFieldOffsetDataStructure;
				PermutationVector.Set< FScreenProbeTraceMeshSDFsCS::FOffsetDataStructure >(GDistanceFieldOffsetDataStructure);
				PermutationVector = FScreenProbeTraceMeshSDFsCS::RemapPermutation(PermutationVector);
				auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeTraceMeshSDFsCS>(PermutationVector);

				ECompactedTracingIndirectArgs IndirectArgsOffset = Lumen::UseThreadGroupSize32() ? ECompactedTracingIndirectArgs::NumTracesDiv32 : ECompactedTracingIndirectArgs::NumTracesDiv64;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("TraceMeshSDFs(%s)", bNeedTraceHairVoxel ? TEXT("Scene, HairStrands") : TEXT("Scene")),
					ComputePassFlags,
					ComputeShader,
					PassParameters,
					CompactedTraceParameters.IndirectArgs,
					(int32)IndirectArgsOffset);
			};

			TraceMeshSDFs();

			bNeedTraceHairVoxel = false;
		}
	}

	FCompactedTraceParameters CompactedTraceParameters = LumenScreenProbeGather::CompactTraces(
		GraphBuilder,
		View,
		ScreenProbeParameters,
		false,
		0.0f,
		FLT_MAX, // Make sure the shader runs on all misses to apply radiance cache + skylight
		/*bCompactForSkyApply*/ true,
		ComputePassFlags);

	auto TraceVoxels = [&]()
	{
		const bool bRadianceCache = LumenScreenProbeGather::UseRadianceCache();

		FScreenProbeTraceVoxelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTraceVoxelsCS::FParameters>();
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		PassParameters->TracingParameters = TracingParameters;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->IndirectTracingParameters = IndirectTracingParameters;
		PassParameters->SceneTexturesStruct = SceneTextures.UniformBuffer;
		PassParameters->CompactedTraceParameters = CompactedTraceParameters;
		if (bNeedTraceHairVoxel)
		{
			PassParameters->HairStrandsVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
		}

		FScreenProbeTraceVoxelsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FThreadGroupSize32 >(Lumen::UseThreadGroupSize32());
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FRadianceCache >(bRadianceCache);
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FScreenProbeExtraAO >(LumenScreenProbeGather::UseScreenProbeExtraAO());
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling(View));
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FHairStrands>(bNeedTraceHairVoxel);
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FTraceVoxels>(!bUseHardwareRayTracing && Lumen::UseGlobalSDFTracing(View.Family->EngineShowFlags));
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FSimpleCoverageBasedExpand>(PermutationVector.Get<FScreenProbeTraceVoxelsCS::FTraceVoxels>() && Lumen::UseGlobalSDFSimpleCoverageBasedExpand());
		PermutationVector = FScreenProbeTraceVoxelsCS::RemapPermutation(PermutationVector);
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeTraceVoxelsCS>(PermutationVector);

		ECompactedTracingIndirectArgs IndirectArgsOffset = Lumen::UseThreadGroupSize32() ? ECompactedTracingIndirectArgs::NumTracesDiv32 : ECompactedTracingIndirectArgs::NumTracesDiv64;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("%s%s", bUseHardwareRayTracing ? TEXT("RadianceCacheInterpolate") : TEXT("TraceVoxels"), bNeedTraceHairVoxel ? TEXT(" and HairStrands") : TEXT("")),
			ComputePassFlags,
			ComputeShader,
			PassParameters,
			CompactedTraceParameters.IndirectArgs,
			(int32)IndirectArgsOffset);
		bNeedTraceHairVoxel = false;
	};

	TraceVoxels();

	if (GLumenScreenProbeGatherVisualizeTraces)
	{
		SetupVisualizeTraces(GraphBuilder, SceneTextures, Scene, View, ScreenProbeParameters, ComputePassFlags);
	}
}

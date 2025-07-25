// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"

#include "CoreMinimal.h"
#include "SpanAllocator.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"

#include "MeshPassProcessor.h"

#include "Experimental/Containers/SherwoodHashTable.h"

#if RHI_RAYTRACING

class FScene;
class FRayTracingScene;

namespace Nanite
{
	class FRayTracingManager : public FRenderResource
	{
	public:
		FRayTracingManager();
		~FRayTracingManager();

		void Initialize();
		void Shutdown();

		virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
		virtual void ReleaseRHI() override;

		void Add(FPrimitiveSceneInfo* SceneInfo);
		void Remove(FPrimitiveSceneInfo* SceneInfo);
		void AddVisiblePrimitive(const FPrimitiveSceneInfo* SceneInfo);

		void RequestUpdates(const TMap<uint32, uint32>& InUpdateRequests);

		void Update();

		// Dispatch compute shader to stream out mesh data for resources with update requests.
		void ProcessUpdateRequests(FRDGBuilder& GraphBuilder, FSceneUniformBuffer &SceneUniformBuffer);

		// Commit pending BLAS builds. This allocates a transient scratch buffer internally.
		bool ProcessBuildRequests(FRDGBuilder& GraphBuilder);

		void EndFrame();

		FRDGBufferSRV* GetAuxiliaryDataSRV(FRDGBuilder& GraphBuilder) const
		{
			return GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(AuxiliaryDataBuffer));
		}

		FRHIRayTracingGeometry* GetRayTracingGeometry(FPrimitiveSceneInfo* SceneInfo) const;

		bool CheckModeChanged();
		float GetCutError() const;

		TRDGUniformBufferRef<FNaniteRayTracingUniformParameters> GetUniformBuffer() const
		{
			return UniformBuffer;
		}

		FRHIUniformBuffer* GetUniformBufferRHI(FRDGBuilder& GraphBuilder)
		{
			return GraphBuilder.ConvertToExternalUniformBuffer(GetUniformBuffer());
		}

		void UpdateUniformBuffer(FRDGBuilder& GraphBuilder, bool bShouldRenderNanite);

	private:

		struct FInternalData
		{
			TSet<FPrimitiveSceneInfo*> Primitives;
			uint32 ResourceId;
			uint32 HierarchyOffset;
			uint32 NumClusters;
			uint32 NumNodes;
			uint32 NumVertices;
			uint32 NumTriangles;
			uint32 NumMaterials;
			uint32 NumSegments;

			uint32 NumResidentClusters;
			uint32 NumResidentClustersUpdate;

			uint32 PrimitiveId;

			TArray<uint32> SegmentMapping;

			FDebugName DebugName;

			FRayTracingGeometryRHIRef RayTracingGeometryRHI;

			uint32 AuxiliaryDataOffset = INDEX_NONE;
			uint32 AuxiliaryDataSize = 0;

			uint32 StagingAuxiliaryDataOffset = INDEX_NONE;
			int32 BaseMeshDataOffset = -1;
			bool bUpdating = false;
		};

		struct FPendingBuild
		{
			FRayTracingGeometryRHIRef RayTracingGeometryRHI;
			uint32 GeometryId;
		};

		TMap<uint32, uint32> ResourceToRayTracingIdMap;
		TSparseArray<FInternalData*> Geometries;

		TSet<uint32> UpdateRequests;
		TSet<uint32> VisibleGeometries;

		TSet<uint32> PendingRemoves;

		TRefCountPtr<FRDGPooledBuffer> AuxiliaryDataBuffer;
		FSpanAllocator AuxiliaryDataAllocator;

		TRefCountPtr<FRDGPooledBuffer> StagingAuxiliaryDataBuffer;

		TRefCountPtr<FRDGPooledBuffer> VertexBuffer;
		TRefCountPtr<FRDGPooledBuffer> IndexBuffer;

		struct FReadbackData
		{
			FRHIGPUBufferReadback* MeshDataReadbackBuffer = nullptr;
			uint32 NumMeshDataEntries = 0;

			TArray<uint32> Entries;
		};

		TArray<FReadbackData> ReadbackBuffers;
		uint32 ReadbackBuffersWriteIndex;
		uint32 ReadbackBuffersNumPending;

		// Geometries to be built this frame
		TArray<uint32> ScheduledBuilds;
		int32 ScheduledBuildsNumPrimitives = 0;

		// Geometries pending BLAS build due to r.RayTracing.Nanite.MaxBlasBuildsPerFrame throttling
		TArray<FPendingBuild> PendingBuilds;

		TRDGUniformBufferRef<FNaniteRayTracingUniformParameters> UniformBuffer;

		const uint32 MaxReadbackBuffers = 4;

		ERayTracingMode bPrevMode = ERayTracingMode::Fallback;
		ERayTracingMode bCurrentMode = ERayTracingMode::Fallback;

		bool bUpdating = false;
		bool bInitialized = false;

#if !UE_BUILD_SHIPPING
		FDelegateHandle ScreenMessageDelegate;

		int32 NumVerticesHighWaterMark = 0;
		int32 NumIndicesHighWaterMark = 0;
		uint64 StagingBufferSizeHighWaterMark = 0;

		int32 NumVerticesHighWaterMarkPrev = 0;
		int32 NumIndicesHighWaterMarkPrev = 0;
		uint64 StagingBufferSizeHighWaterMarkPrev = 0;
#endif // UE_BUILD_SHIPPING
	};

	extern TGlobalResource<FRayTracingManager> GRayTracingManager;

} // namespace Nanite

#endif // RHI_RAYTRACING

// -------------------------------------------------------------------------
// PointCloud::Data — ECS component for PropertySet-backed point cloud
//                    rendering (Cloud source).
// -------------------------------------------------------------------------
//
// Holds a shared_ptr to an authoritative Geometry::PointCloud::Cloud
// instance. Positions, normals, colors, and radii are sourced directly
// from the Cloud's PropertySets — no std::vector copies on the component.
//
// Rendering: retained-mode via BDA shared-buffer architecture.
//   - Points rendered via PointPass (BDA position pull).
//   - GPU state managed by PointCloudLifecycleSystem: positions/normals
//     are uploaded to a device-local vertex buffer (GpuGeometry), per-point
//     attributes extracted from PropertySets.
//   - Re-upload triggers on GpuDirty = true.
//
// Two creation paths:
//   a) Cloud-backed: CloudRef is populated by loaders/algorithms, GpuDirty=true.
//   b) Preloaded geometry-backed: GpuGeometry is pre-populated (e.g., model point
//      topology), CloudRef may be null, GpuDirty=false.
//
// PointCloud::Data is the single point-cloud ECS data path.
// It supports both Cloud-backed entities and preloaded GPU point topologies.

module;
#include <cstdint>
#include <memory>
#include <vector>

#ifdef INTRINSIC_HAS_CUDA
#include <cuda.h>
#endif

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

export module Graphics.Components.PointCloud;

import Graphics.Components.Core;
import Graphics.VisualizationConfig;
import Geometry.PointCloudUtils;
import Geometry.Handle;
import Geometry.KMeans;
#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
#endif

export namespace ECS::PointCloud
{
    struct Data
    {
        // ---- Authoritative Data Source ----
        std::shared_ptr<Geometry::PointCloud::Cloud> CloudRef;

        // ---- Rendering Parameters (not data — data lives in PropertySets) ----
        Geometry::PointCloud::RenderMode RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        float     DefaultRadius    = 0.005f;         // World-space radius.
        float     SizeMultiplier   = 1.0f;           // Per-entity size multiplier.
        glm::vec4 DefaultColor     = {1.f, 1.f, 1.f, 1.f}; // RGBA when colors absent.
        bool      Visible          = true;            // Runtime visibility toggle.

        // ---- Visualization Configuration ----
        // Selects which PropertySet properties drive per-point color rendering.
        // When VertexColors.PropertyName is empty, falls back to Cloud::Colors().
        Graphics::VisualizationConfig Visualization;

        // ---- GPU State (managed by PointCloudLifecycleSystem) ----
        // Device-local vertex buffer holding positions + normals.
        // PointPass reads from this buffer via BDA.
        Geometry::GeometryHandle GpuGeometry{};

        // GPUScene slot for frustum culling and GPU-driven batching.
        // Allocated by PointCloudLifecycleSystem after successful upload.
        // Freed by on_destroy hook in SceneManager.
        uint32_t GpuSlot = ECS::kInvalidGpuSlot;

        // Per-point colors (packed ABGR), one per point.
        // Extracted from Cloud's "p:color" by PointCloudLifecycleSystem.
        // When empty, PointPass uses uniform DefaultColor.
        std::vector<uint32_t> CachedColors;

        // Per-point radii (world-space), one per point.
        // Extracted from Cloud's "p:radius" by PointCloudLifecycleSystem.
        // When empty, PointPass uses uniform DefaultRadius.
        std::vector<float> CachedRadii;

        // When true, PointCloudLifecycleSystem re-uploads positions/normals
        // and re-extracts per-point attributes. Set on first attach, or when
        // Cloud data changes.
        bool GpuDirty = true;

        // Point count in the GPU buffer (matches Cloud::Size() at upload time).
        uint32_t GpuPointCount = 0;

        // When geometry is pre-populated without CloudRef, this tracks whether
        // the uploaded GPU layout contains normals (Surfel/EWA eligibility).
        bool HasGpuNormals = false;

        // ---- K-means compute state ----
        // Monotonic position revision used by async clustering to avoid
        // redundant host->CUDA uploads when point positions did not change.
        uint64_t PositionRevision = 1;

        bool KMeansJobPending = false;
        uint32_t KMeansPendingClusterCount = 0;
        Geometry::KMeans::Backend KMeansLastBackend = Geometry::KMeans::Backend::CPU;
        uint32_t KMeansLastIterations = 0;
        bool KMeansLastConverged = false;
        float KMeansLastInertia = 0.0f;
        uint32_t KMeansLastMaxDistanceIndex = 0;
        double KMeansLastDurationMs = 0.0;
        entt::entity KMeansCentroidEntity = entt::null;
        uint64_t KMeansResultRevision = 0;

#ifdef INTRINSIC_HAS_CUDA
        // Persistent CUDA resources kept alive per entity until explicit release
        // or PointCloud::Data destruction.
        RHI::CudaBufferHandle CudaPositions{};
        RHI::CudaBufferHandle CudaLabels{};
        RHI::CudaBufferHandle CudaDistances{};
        RHI::CudaBufferHandle CudaCentroids{};
        RHI::CudaBufferHandle CudaSums{};
        RHI::CudaBufferHandle CudaClusterSizes{};
        CUstream CudaStream = nullptr;
        CUevent CudaStartEvent = nullptr;
        CUevent CudaCompletionEvent = nullptr;
        uint32_t CudaPointCapacity = 0;
        uint32_t CudaClusterCapacity = 0;
        uint64_t CudaPositionRevision = 0;

        void ReleaseCudaBuffers(RHI::CudaDevice& cudaDevice)
        {
            if (CudaStream)
            {
                cudaDevice.DestroyStream(CudaStream);
                CudaStream = nullptr;
            }

            if (CudaStartEvent)
            {
                cudaDevice.DestroyEvent(CudaStartEvent);
                CudaStartEvent = nullptr;
            }
            if (CudaCompletionEvent)
            {
                cudaDevice.DestroyEvent(CudaCompletionEvent);
                CudaCompletionEvent = nullptr;
            }

            cudaDevice.FreeBuffer(CudaPositions);
            cudaDevice.FreeBuffer(CudaLabels);
            cudaDevice.FreeBuffer(CudaDistances);
            cudaDevice.FreeBuffer(CudaCentroids);
            cudaDevice.FreeBuffer(CudaSums);
            cudaDevice.FreeBuffer(CudaClusterSizes);

            CudaPointCapacity = 0;
            CudaClusterCapacity = 0;
            CudaPositionRevision = 0;
            KMeansJobPending = false;
            KMeansPendingClusterCount = 0;
        }
#endif

        // ---- Queries (delegate to CloudRef) ----
        [[nodiscard]] std::size_t PointCount() const noexcept
        {
            return CloudRef ? CloudRef->VerticesSize() : 0;
        }
        [[nodiscard]] bool HasNormals() const noexcept
        {
            return CloudRef && CloudRef->HasNormals();
        }
        [[nodiscard]] bool HasRenderableNormals() const noexcept
        {
            return HasNormals() || HasGpuNormals;
        }
        [[nodiscard]] bool HasColors() const noexcept
        {
            return CloudRef && CloudRef->HasColors();
        }
        [[nodiscard]] bool HasRadii() const noexcept
        {
            return CloudRef && CloudRef->HasRadii();
        }
        [[nodiscard]] bool HasGpuGeometry() const noexcept
        {
            return GpuGeometry.IsValid();
        }
    };
}

module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

export module Extrinsic.Runtime.RenderExtraction;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Light;
import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;
import Extrinsic.Runtime.SpatialDebugAdapters;

export namespace Extrinsic::Runtime
{
    enum class RuntimeRenderableAssetObservationStatus : std::uint8_t
    {
        NoSourceAsset,
        CacheUnavailable,
        ViewUnavailable,
        GenerationUnavailable,
        UpToDate,
        RebindRequired,
    };

    struct RuntimeRenderableAssetGenerationObservation
    {
        RuntimeRenderableAssetObservationStatus Status = RuntimeRenderableAssetObservationStatus::NoSourceAsset;
        Graphics::Components::GpuSceneSlotAssetRebindDecision Decision =
            Graphics::Components::GpuSceneSlotAssetRebindDecision::NoSourceAsset;
        Assets::AssetId SourceAsset{};
        std::uint64_t ObservedGeneration = 0;
    };

    enum class RuntimeRenderableAssetAcknowledgmentResult : std::uint8_t
    {
        Acknowledged,
        SkippedNoSourceAsset,
        SkippedAssetMismatch,
        SkippedNoObservedGeneration,
    };

    struct RuntimeRenderExtractionStats
    {
        std::uint32_t CandidateRenderableCount{0};
        std::uint32_t SubmittedTransformCount{0};
        std::uint32_t SubmittedVisualizationCount{0};
        std::uint32_t SubmittedLightCount{0};
        std::uint32_t AllocatedInstanceCount{0};
        std::uint32_t FreedInstanceCount{0};
        std::uint32_t DirtyTransformCount{0};
        std::uint32_t SkippedInvalidEntityCount{0};
        std::uint32_t SourceAssetObservationCount{0};
        std::uint32_t SourceAssetCacheUnavailableCount{0};
        std::uint32_t SourceAssetViewUnavailableCount{0};
        std::uint32_t SourceAssetGenerationUnavailableCount{0};
        std::uint32_t SourceAssetUpToDateCount{0};
        std::uint32_t SourceAssetRebindRequiredCount{0};
        std::uint32_t SourceAssetRebindAcknowledgedCount{0};
        std::uint32_t ProceduralRenderablesEnumerated{0};
        std::uint32_t ProceduralGeometryUploads{0};
        std::uint32_t ProceduralGeometryReuseHits{0};
        std::uint32_t ProceduralGeometryFailedPack{0};
        std::uint32_t ProceduralGeometryMissingPacker{0};
        std::uint32_t ProceduralGeometryInvalidParams{0};
        std::uint32_t ProceduralAndAssetSourceConflict{0};
        std::uint32_t ProceduralAndRenderableSourceConflict{0};
        std::uint32_t ProceduralGeometryReleases{0};
        std::uint32_t ProceduralGeometryFreeRetires{0};
        std::uint32_t ProceduralGeometryRetireCancellations{0};
        std::uint32_t ProceduralGeometryRefCountSaturated{0};

        // RUNTIME-085 Slices B/C — runtime-authored mesh `GeometrySources`
        // residency counters. `Uploads` is incremented exactly once per
        // entity on the first frame the mesh is packed and uploaded;
        // subsequent clean frames hit `ReuseHits`; subsequent dirty
        // frames hit `Reuploads` after Slice C drains the dirty-domain
        // tags (`DirtyVertexPositions`, `DirtyFaceTopology`,
        // `DirtyEdgeTopology`, `GpuDirty`) and repacks the mesh.
        // `FailedPack` aggregates non-input-shape pack rejections
        // (`InvalidTopology`, `DegenerateAllFaces`, `EmptyMesh`,
        // `NonFinitePosition`, `MissingHalfedgeTopology`,
        // `MissingFaceTopology`, `WrongDomain`); `MissingPositions` and
        // `InvalidTopology` get their own counters because they are the
        // two most likely structural authoring bugs in mesh sources.
        // `Releases` is incremented per release-initiated event: entity
        // destruction, eligibility flip away from mesh, or dirty
        // reupload superseding an older handle; the actual free runs
        // through the `framesInFlight` deferred-retire window driven by
        // `TickMeshGeometry`, which surfaces `FreeRetires` as a per-frame
        // delta on the next `ExtractAndSubmit` (mirroring
        // `ProceduralGeometryFreeRetires`).
        std::uint32_t MeshGeometryUploads{0};
        std::uint32_t MeshGeometryReuseHits{0};
        std::uint32_t MeshGeometryReuploads{0};
        std::uint32_t MeshGeometryFailedPack{0};
        std::uint32_t MeshGeometryMissingPositions{0};
        std::uint32_t MeshGeometryInvalidTopology{0};
        std::uint32_t MeshGeometryReleases{0};
        std::uint32_t MeshGeometryFreeRetires{0};

        // RUNTIME-082 Slice D — spatial-debug adapter pump counters. Folded
        // per-frame from the active adapter set against the entity view of
        // `ECS::Components::SpatialDebugBinding`. The accumulator fields
        // mirror `SpatialDebugAdapterStats` summed across every invoked
        // adapter; the count fields mirror the per-frame submitted batch
        // span sizes (so they remain consistent with what the renderer
        // sees in `RuntimeRenderSnapshotBatch`).
        std::uint32_t SpatialDebugBindingsObserved{0};
        std::uint32_t SpatialDebugAdaptersInvoked{0};
        std::uint32_t SpatialDebugMissingAdapterCount{0};
        std::uint32_t SpatialDebugBoundsCount{0};
        std::uint32_t SpatialDebugHierarchyNodeCount{0};
        std::uint32_t SpatialDebugSplitPlaneCount{0};
        std::uint32_t SpatialDebugConvexHullVertexCount{0};
        std::uint32_t SpatialDebugConvexHullEdgeCount{0};
        std::uint32_t SpatialDebugPointMarkerCount{0};
        std::uint32_t SpatialDebugLeafNodeAccumulator{0};
        std::uint32_t SpatialDebugInnerNodeAccumulator{0};
        std::uint32_t SpatialDebugEmptyNodeSkippedAccumulator{0};
        std::uint32_t SpatialDebugDepthCapTruncationAccumulator{0};
    };

    [[nodiscard]] RuntimeRenderableAssetGenerationObservation ObserveRenderableAssetGeneration(
        Graphics::Components::GpuSceneSlot& slot,
        Assets::AssetId sourceAsset,
        Graphics::GpuAssetCache* gpuAssets);

    [[nodiscard]] RuntimeRenderableAssetAcknowledgmentResult AcknowledgeRenderableAssetRebind(
        Graphics::Components::GpuSceneSlot& slot,
        const RuntimeRenderableAssetGenerationObservation& observation) noexcept;

    class RenderExtractionCache
    {
    public:
        RenderExtractionCache() = default;
        ~RenderExtractionCache() = default;

        RenderExtractionCache(const RenderExtractionCache&) = delete;
        RenderExtractionCache& operator=(const RenderExtractionCache&) = delete;

        [[nodiscard]] RuntimeRenderExtractionStats ExtractAndSubmit(ECS::Scene::Registry& scene,
                                                                    Graphics::IRenderer& renderer,
                                                                    Graphics::GpuAssetCache* gpuAssets = nullptr);
        void Shutdown(Graphics::IRenderer& renderer);

        // Maintenance-phase hook called by Engine::RunFrame after
        // ExtractAndSubmit and Renderer::Present.  Drives the deferred-retire
        // window of the procedural geometry cache using the same frame
        // counter and framesInFlight that the runtime hands to
        // `Graphics::GpuAssetCache::Tick`.
        void TickProceduralGeometry(std::uint64_t currentFrame,
                                    std::uint32_t framesInFlight,
                                    Graphics::IRenderer& renderer);

        // RUNTIME-085 Slice C — drives the deferred-retire window of the
        // runtime-owned mesh-residency retire queue, mirroring
        // `TickProceduralGeometry`. Handles enqueued by entity destruction,
        // eligibility flip, or dirty reupload are freed via
        // `GpuWorld::FreeGeometry` once `framesInFlight` ticks have elapsed
        // since the release tick. Subsequent `ExtractAndSubmit` calls surface
        // the per-tick delta as `MeshGeometryFreeRetires`.
        void TickMeshGeometry(std::uint64_t currentFrame,
                              std::uint32_t framesInFlight,
                              Graphics::IRenderer& renderer);

        [[nodiscard]] const RuntimeRenderExtractionStats& GetLastStats() const noexcept;
        [[nodiscard]] std::uint32_t GetTrackedRenderableCount() const noexcept;

        struct RenderableSidecarView
        {
            Graphics::GpuInstanceHandle Instance{};
            Graphics::GpuGeometryHandle Geometry{};
            std::optional<ProceduralGeometryKey> ProceduralKey{};
            bool HasSourceAsset = false;
            std::uint32_t GeometrySlot = 0;
            std::uint32_t GeometryGeneration = 0;
            // RUNTIME-085 Slice B — runtime-authored mesh-source residency.
            // `MeshGeometry` is the handle the cache owns and frees on
            // retirement; it is distinct from the procedural cache's
            // refcounted handle and from any asset-backed geometry that
            // `GpuSlot.SourceAsset` may later wire in.
            Graphics::GpuGeometryHandle MeshGeometry{};
            bool HasMeshResidency = false;
        };

        [[nodiscard]] std::optional<RenderableSidecarView> FindRenderableSidecarForTest(
            std::uint32_t stableEntityId) const noexcept;

        [[nodiscard]] const ProceduralGeometryCache& GetProceduralGeometryCacheForTest() const noexcept;

        // RUNTIME-082 Slice D — adapter-ownership surface.
        //
        // The cache owns adapter instances via `std::unique_ptr` and mirrors
        // each registration into an embedded `SpatialDebugAdapterRegistry`
        // that the extraction pump consults per
        // `ECS::Components::SpatialDebugBinding`. Callers transfer adapter
        // ownership at registration time; `Unregister` destroys the adapter
        // instance and clears the registry slot. Re-registering an existing
        // key replaces (and destroys) the prior adapter.
        //
        // The source geometry tree the adapter wraps remains caller-owned;
        // callers must `UnregisterSpatialDebugAdapter` before the source tree
        // is destroyed to avoid the adapter dereferencing freed memory on
        // the next `ExtractAndSubmit`.
        void RegisterSpatialDebugAdapter(std::uint64_t key,
                                          std::unique_ptr<ISpatialDebugAdapter> adapter);
        bool UnregisterSpatialDebugAdapter(std::uint64_t key) noexcept;
        [[nodiscard]] std::size_t GetSpatialDebugAdapterCount() const noexcept;
        [[nodiscard]] const SpatialDebugAdapterRegistry& GetSpatialDebugRegistryForTest() const noexcept;

    private:
        struct RenderableSidecar
        {
            Graphics::GpuInstanceHandle Instance{};
            Graphics::Components::GpuSceneSlot GpuSlot{};
            Graphics::Components::MaterialInstance Material{};
            Graphics::Components::VisualizationConfig Visualization{};
            bool HasVisualization{false};
            Graphics::GpuGeometryHandle Geometry{};
            std::optional<ProceduralGeometryKey> ProceduralKey{};
            // RUNTIME-085 Slice B — owned mesh-source residency handle.
            // Distinct from `Geometry` (which mirrors the currently bound
            // instance geometry) so retirement can free the runtime-owned
            // upload even after a Slice C reupload swaps `Geometry`.
            Graphics::GpuGeometryHandle MeshGeometry{};
        };

        [[nodiscard]] RenderableSidecar* EnsureRenderable(std::uint32_t stableId,
                                                          Graphics::IRenderer& renderer,
                                                          RuntimeRenderExtractionStats& stats);
        void RetireMissingRenderables(const std::unordered_set<std::uint32_t>& liveKeys,
                                      Graphics::IRenderer& renderer,
                                      RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindProceduralGeometry(const ECS::Components::ProceduralGeometryRef& ref,
                                                   RenderableSidecar& sidecar,
                                                   Graphics::IRenderer& renderer,
                                                   RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindMeshGeometry(entt::registry& registry,
                                             entt::entity entity,
                                             const ECS::Components::GeometrySources::ConstSourceView& view,
                                             RenderableSidecar& sidecar,
                                             Graphics::IRenderer& renderer,
                                             RuntimeRenderExtractionStats& stats);

        // RUNTIME-085 Slice C — runtime-owned deferred-retire queue for mesh
        // upload handles. Mirrors the shape of
        // `ProceduralGeometryCache::RetireRecord` but without a refcounted key
        // because mesh uploads are not shared across entities. A record is
        // enqueued with `DeadlineSet = false`; the next `TickMeshGeometry`
        // anchors `Deadline = currentFrame + framesInFlight` and frees the
        // handle once `Deadline <= currentFrame`.
        struct MeshRetireRecord
        {
            Graphics::GpuGeometryHandle Handle{};
            std::uint64_t Deadline = 0;
            bool DeadlineSet = false;
        };

        void EnqueueMeshRetire(Graphics::GpuGeometryHandle handle);

        std::unordered_map<std::uint32_t, RenderableSidecar> m_Renderables{};
        std::vector<Graphics::TransformSyncRecord> m_Transforms{};
        std::vector<Graphics::VisualizationSyncRecord> m_Visualizations{};
        std::vector<Graphics::LightSnapshot> m_Lights{};
        ProceduralGeometryCache m_ProceduralGeometry{};
        ProceduralGeometryPackBuffer m_ProceduralPack{};
        ProceduralGeometryCacheStats m_PrevProceduralStats{};
        MeshPackBuffer m_MeshPack{};

        // RUNTIME-085 Slice C — deferred-retire queue + running FreeRetires
        // accumulator. `m_PrevMeshFreeRetires` lets `ExtractAndSubmit` emit
        // `MeshGeometryFreeRetires` as a per-tick delta, matching the
        // `ProceduralGeometryFreeRetires` accounting path.
        std::vector<MeshRetireRecord> m_MeshRetire{};
        std::uint32_t m_MeshFreeRetires{0};
        std::uint32_t m_PrevMeshFreeRetires{0};

        // RUNTIME-082 Slice D — owned adapter instances + a registry mirror
        // resolved per-entity by `ExtractAndSubmit`. The batch buffer is
        // cleared per frame and its spans are attached to
        // `RuntimeRenderSnapshotBatch::SpatialDebug*` for the renderer.
        std::unordered_map<std::uint64_t, std::unique_ptr<ISpatialDebugAdapter>> m_SpatialDebugAdapters{};
        SpatialDebugAdapterRegistry m_SpatialDebugRegistry{};
        SpatialDebugSnapshotBatch m_SpatialDebugBatch{};

        RuntimeRenderExtractionStats m_LastStats{};
    };
}

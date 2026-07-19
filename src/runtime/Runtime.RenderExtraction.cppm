module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Extrinsic.Runtime.RenderExtraction;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.Component.GpuSceneSlot;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.SpatialDebugAdapters;
import Extrinsic.Runtime.WorldHandle;
export import Extrinsic.Runtime.VisualizationAdapters;

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
        WorldHandle World{DefaultWorldHandle};
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
        // subsequent clean frames hit `ReuseHits`; vertex-channel dirty
        // frames hit `Reuploads` plus `PartialUploads` after only the changed
        // SoA channel is re-written, while topology/coarse GPU dirty frames
        // fall back to full pack-and-replace reuploads.
        // `FailedPack` aggregates non-input-shape pack rejections
        // (`DegenerateAllFaces`, `EmptyMesh`, `NonFinitePosition`,
        // `MissingHalfedgeTopology`, `MissingFaceTopology`, `WrongDomain`);
        // `MissingPositions`, `InvalidTopology`, and texture-coordinate
        // failures get their own counters because they are the most likely
        // structural authoring bugs in mesh sources.
        // `Releases` is incremented per release-initiated event: entity
        // destruction, eligibility flip away from mesh, or dirty
        // full reupload superseding an older handle; partial channel uploads
        // keep the resident handle. The actual free runs through the
        // `framesInFlight` deferred-retire window driven by `TickMeshGeometry`,
        // which surfaces `FreeRetires` as a per-frame delta on the next
        // `ExtractAndSubmit` (mirroring `ProceduralGeometryFreeRetires`).
        std::uint32_t MeshGeometryUploads{0};
        std::uint32_t MeshGeometryReuseHits{0};
        std::uint32_t MeshGeometryReuploads{0};
        std::uint32_t MeshGeometryPartialUploads{0};
        std::uint32_t MeshGeometryFailedPack{0};
        std::uint32_t MeshGeometryMissingPositions{0};
        std::uint32_t MeshGeometryInvalidTopology{0};
        std::uint32_t MeshGeometryMissingTexcoords{0};
        std::uint32_t MeshGeometryNonFiniteTexcoords{0};
        std::uint32_t MeshGeometryReleases{0};
        std::uint32_t MeshGeometryFreeRetires{0};

        // RUNTIME-086 Slices B/C — runtime-authored graph `GeometrySources`
        // residency counters, mirroring the mesh accounting above. A graph
        // entity carrying `RenderEdges` and/or `RenderPoints` packs its node
        // positions (shared vertex buffer) plus optional `(e:v0, e:v1)` line
        // indices into one `GpuGeometryHandle`. `Uploads` is incremented once
        // per entity on the first frame the graph is packed and uploaded;
        // clean frames hit `ReuseHits`; vertex-channel dirty frames hit
        // `Reuploads` plus `PartialUploads` and keep the resident handle;
        // edge-topology/coarse GPU dirty frames fall back to full
        // pack-and-replace reuploads. `MissingNodes` aggregates the two
        // node-shape pack rejections (`MissingNodes`, `EmptyGraph`) and
        // `InvalidEdges` the out-of-range edge endpoint rejection because those
        // are the likeliest structural authoring bugs in graph sources; every
        // other non-`Success` status (`WrongDomain`, `NoRenderLane`,
        // `MissingEdgeTopology`, `NonFinitePosition`) folds into `FailedPack`.
        // `Releases` is incremented per release-initiated event (entity
        // destruction, eligibility flip away from graph, or full dirty
        // reupload superseding an older handle); partial channel uploads keep
        // the resident handle. The actual free runs through the
        // `framesInFlight` deferred-retire window driven by `TickGraphGeometry`,
        // which surfaces `FreeRetires` as a per-frame delta on the next
        // `ExtractAndSubmit` (mirroring `MeshGeometryFreeRetires`).
        std::uint32_t GraphGeometryUploads{0};
        std::uint32_t GraphGeometryReuseHits{0};
        std::uint32_t GraphGeometryReuploads{0};
        std::uint32_t GraphGeometryPartialUploads{0};
        std::uint32_t GraphGeometryFailedPack{0};
        std::uint32_t GraphGeometryMissingNodes{0};
        std::uint32_t GraphGeometryInvalidEdges{0};
        std::uint32_t GraphGeometryReleases{0};
        std::uint32_t GraphGeometryFreeRetires{0};

        // RUNTIME-087 — runtime-authored point-cloud `GeometrySources`
        // residency counters, mirroring the mesh/graph accounting above. A
        // point-cloud entity carrying `RenderPoints` packs its `v:position`
        // rows into one `GpuGeometryHandle` (positions only — no index buffer,
        // no line lane). `Uploads` is incremented once per entity on the first
        // frame the cloud is packed and uploaded; clean frames hit `ReuseHits`;
        // vertex-channel dirty frames hit `Reuploads` plus `PartialUploads` and
        // keep the resident handle; coarse GPU dirty frames fall back to full
        // pack-and-replace reuploads. `MissingPositions` aggregates the
        // two position-shape pack rejections (`MissingPositions`, `EmptyCloud`)
        // and `InvalidPoints` the non-finite-position rejection because those
        // are the likeliest structural authoring bugs in cloud sources; every
        // other bind-level rejection (`WrongDomain`, unsupported
        // `RenderSurface`/`RenderEdges` requests, plus an unsupported per-point
        // `RenderPoints::SizeSource` buffer variant — only a uniform float
        // point size is supported in this slice) folds into `FailedPack`.
        // `Releases` is incremented per release-initiated event (entity
        // destruction, eligibility flip away from point-cloud, or full dirty
        // reupload superseding an older handle); partial channel uploads keep
        // the resident handle. The actual free runs through the `framesInFlight`
        // deferred-retire window driven by `TickPointCloudGeometry`, which
        // surfaces `FreeRetires` as a per-frame delta on the next
        // `ExtractAndSubmit` (mirroring `GraphGeometryFreeRetires`).
        std::uint32_t PointCloudGeometryUploads{0};
        std::uint32_t PointCloudGeometryReuseHits{0};
        std::uint32_t PointCloudGeometryReuploads{0};
        std::uint32_t PointCloudGeometryPartialUploads{0};
        std::uint32_t PointCloudGeometryFailedPack{0};
        std::uint32_t PointCloudGeometryMissingPositions{0};
        std::uint32_t PointCloudGeometryInvalidPoints{0};
        std::uint32_t PointCloudGeometryReleases{0};
        std::uint32_t PointCloudGeometryFreeRetires{0};

        // RUNTIME-106 — runtime-owned mesh *primitive view* residency counters.
        // A mesh entity that carries `RenderEdges` and/or `RenderPoints`
        // derives one extra retained renderable per requested lane from the
        // same authoritative mesh `GeometrySources`: the edge view binds a
        // line-list (`GpuRender_Line`) and the vertex view a point list
        // (`GpuRender_Point`), each through its own `GpuWorld` instance +
        // `GpuGeometryHandle` recorded in the mesh entity's sidecar. These
        // sidecars do not require `RenderSurface`; surface, edge, and point
        // lanes compose independently. Each view is a single-owner residency
        // stream: `Uploads` once on the frame a view is first created,
        // `ReuseHits` on clean frames, `Reuploads` when position/topology
        // mesh-source data changes, `Releases` per release-initiated event
        // (component removed, entity flips away from mesh, full dirty reupload
        // superseding an older handle, entity destruction, or shutdown). The
        // edge view folds `MissingPositions`/`EmptyMesh` into
        // `MissingPositions`, reports `MissingEdgeTopology` and out-of-range
        // endpoints in their own counters, and folds every other rejection into
        // `FailedPack`; the vertex view has no edge topology, so only
        // `MissingPositions` and `FailedPack` apply. Both views share one
        // `framesInFlight` deferred-retire queue driven by
        // `TickMeshPrimitiveViewGeometry`, which surfaces the per-tick free
        // delta as the shared `MeshPrimitiveViewFreeRetires`.
        std::uint32_t MeshEdgeViewUploads{0};
        std::uint32_t MeshEdgeViewReuseHits{0};
        std::uint32_t MeshEdgeViewReuploads{0};
        std::uint32_t MeshEdgeViewReleases{0};
        std::uint32_t MeshEdgeViewFailedPack{0};
        std::uint32_t MeshEdgeViewMissingPositions{0};
        std::uint32_t MeshEdgeViewMissingEdgeTopology{0};
        std::uint32_t MeshEdgeViewInvalidEdges{0};
        std::uint32_t MeshVertexViewUploads{0};
        std::uint32_t MeshVertexViewReuseHits{0};
        std::uint32_t MeshVertexViewReuploads{0};
        std::uint32_t MeshVertexViewReleases{0};
        std::uint32_t MeshVertexViewFailedPack{0};
        std::uint32_t MeshVertexViewMissingPositions{0};
        std::uint32_t MeshPrimitiveViewFreeRetires{0};

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

        // RUNTIME-083 Slices B/E — runtime visualization adapter pump counters.
        // `VisualizationAdapterScalarConfigsObserved` preserves the original
        // scalar-field config count. Slice E extends the same binding surface to
        // non-scalar adapter packets; the missing/invalid counters are folded
        // from `VisualizationAdapterStats`, and packet lane counts mirror the
        // spans attached to `RuntimeRenderSnapshotBatch::Visualization*`.
        std::uint32_t VisualizationAdapterScalarConfigsObserved{0};
        std::uint32_t VisualizationAdapterBindingsMissing{0};
        std::uint32_t VisualizationAdapterMissingAdapterCount{0};
        std::uint32_t VisualizationAdapterInvokedCount{0};
        std::uint32_t VisualizationAdapterPacketAppendCount{0};
        std::uint32_t VisualizationAdapterMissingSourceCount{0};
        std::uint32_t VisualizationAdapterUnsupportedSourceTypeCount{0};
        std::uint32_t VisualizationAdapterEmptySourceCount{0};
        std::uint32_t VisualizationAdapterInvalidBufferCount{0};
        std::uint32_t VisualizationAdapterInvalidRangeCount{0};
        std::uint32_t VisualizationAdapterNonFiniteValueCount{0};
        std::uint32_t VisualizationAdapterElementCountOverflowCount{0};
        std::uint32_t VisualizationAdapterManualRangeCount{0};
        std::uint32_t VisualizationAdapterFlatAutoRangeExpandedCount{0};
        std::uint32_t VisualizationAdapterRobustAutoRangeClampedCount{0};
        std::uint64_t VisualizationAdapterScalarValueScanCount{0};
        std::uint32_t VisualizationAttributeBufferPacketCount{0};
        std::uint32_t VisualizationScalarPacketCount{0};
        std::uint32_t VisualizationColorPacketCount{0};
        std::uint32_t VisualizationVectorFieldPacketCount{0};
        std::uint32_t VisualizationIsolinePacketCount{0};
        std::uint32_t VisualizationHtexAtlasPacketCount{0};
        std::uint32_t VisualizationFragmentBakeAtlasPacketCount{0};

        // GRAPHICS-036B — read-only mirror of the runtime `RenderWorldPool`
        // diagnostics (GRAPHICS-036 decision 7). The pool (`GRAPHICS-036A`) owns
        // the authoritative atomic counters; these fields surface them on the
        // runtime extraction diagnostics so editor overlays / frame-stat readers
        // see pipeline back-pressure without reaching into the pool. They stay
        // zero until `GRAPHICS-036C` wires the pool into the frame loop and calls
        // `MirrorRenderWorldPoolDiagnostics`. `RenderWorldFrameAgeFrames` is the
        // age, in frames, of the most recently consumed snapshot (0 in
        // synchronous mode); a bucketed-histogram form is deferred to a follow-up
        // and is not owed by this slice.
        std::uint64_t RenderWorldPipelineStallCount{0};
        std::uint64_t RenderWorldExtractionSkipCount{0};
        std::uint64_t RenderWorldFrameAgeFrames{0};

        // ASSETIO-007 — data-only material texture bindings registered by
        // runtime import paths and resolved against extraction-owned material
        // sidecars. Failures are retryable because the texture asset may still
        // be pending or the CPU/null gate may have no fallback texture.
        std::uint32_t MaterialTextureBindingRecordCount{0};
        std::uint32_t MaterialTextureBindingResolveCount{0};
        std::uint32_t MaterialTextureBindingResolveFailureCount{0};

        // RUNTIME-113 — progressive descriptor consumption during extraction.
        // These counters are backend-neutral: they report descriptor readiness,
        // fallback/default use, property-buffer availability, and material
        // texture binding attempts without exposing ECS/property pointers or GPU
        // handles to graphics snapshots.
        std::uint32_t ProgressivePresentationEntityCount{0};
        std::uint32_t ProgressivePresentationLaneCount{0};
        std::uint32_t ProgressivePresentationSlotCount{0};
        std::uint32_t ProgressiveDefaultSlotCount{0};
        std::uint32_t ProgressiveReadyTextureSlotCount{0};
        std::uint32_t ProgressivePropertyBufferReadyCount{0};
        std::uint32_t ProgressivePendingSlotCount{0};
        std::uint32_t ProgressiveUnsupportedSlotCount{0};
        std::uint32_t ProgressivePreviousOutputRetainedCount{0};
        std::uint32_t ProgressiveDiagnosticCount{0};
        std::uint32_t ProgressiveMaterialTextureBindingResolveCount{0};
        std::uint32_t ProgressiveMaterialTextureBindingResolveFailureCount{0};
    };

    // RUNTIME-188 — copied, world-tagged interaction data consumed by render
    // extraction. The value owns all of its storage so Graphics never borrows
    // pointers into an optional runtime module.
    struct RuntimeSceneInteractionRenderSnapshot
    {
        WorldHandle World{};
        std::vector<std::uint32_t> SelectedRenderIds{};
        bool HasHovered{false};
        std::uint32_t HoveredRenderId{0u};
        std::vector<Graphics::TransformGizmoRenderPacket> GizmoDrawPackets{};
    };

    // GRAPHICS-036B — copy the authoritative `RenderWorldPool` diagnostics
    // (`GRAPHICS-036A`) into the runtime extraction stats mirror. Pure and
    // side-effect-free apart from writing the three `RenderWorld*` fields, so the
    // caller controls cadence. `GRAPHICS-036C` calls this once the pool is wired.
    void MirrorRenderWorldPoolDiagnostics(const RenderWorldPool& pool,
                                          RuntimeRenderExtractionStats& stats) noexcept;

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
        RenderExtractionCache();
        ~RenderExtractionCache();

        RenderExtractionCache(const RenderExtractionCache&) = delete;
        RenderExtractionCache& operator=(const RenderExtractionCache&) = delete;

        // Frame extraction: read the live ECS scene and publish an immutable
        // snapshot batch for the renderer without handing ECS references to
        // graphics. Phases in frame order: per-entity light extraction;
        // per-entity renderable reconciliation (semantic filtering + residency
        // reconciliation — see `ReconcileRenderableEntity`); retirement of
        // renderables that left the live set; spatial-debug adapter pump;
        // stats finalization + snapshot submit. View-frustum/HZB/occlusion
        // culling is not extraction's job — it stays on the graphics side in
        // `Graphics::CullingSystem`.
        //
        // Optional interaction state is submitted separately as a copied,
        // world-tagged value. Omission or a world mismatch leaves selection,
        // hover, and gizmo lanes empty.
        [[nodiscard]] RuntimeRenderExtractionStats ExtractAndSubmit(
            ECS::Scene::Registry& scene,
            Graphics::IRenderer& renderer,
            Graphics::GpuAssetCache* gpuAssets = nullptr,
            std::uint32_t runtimeSnapshotStorageSlot = 0u,
            WorldHandle world = DefaultWorldHandle);
        void SubmitSceneInteractionSnapshot(
            const RuntimeSceneInteractionRenderSnapshot& snapshot);
        // Scene replacement boundary: free scene-owned renderable sidecars,
        // collapse deferred retire queues, clear per-entity extraction settings
        // and bindings, and submit an empty snapshot. Adapter registrations stay
        // live because they are runtime/editor resources, not scene contents.
        void ClearSceneState(Graphics::IRenderer& renderer);
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
        // eligibility flip, or full dirty reupload are freed via
        // `GpuWorld::FreeGeometry` once `framesInFlight` ticks have elapsed
        // since the release tick. Subsequent `ExtractAndSubmit` calls surface
        // the per-tick delta as `MeshGeometryFreeRetires`.
        void TickMeshGeometry(std::uint64_t currentFrame,
                              std::uint32_t framesInFlight,
                              Graphics::IRenderer& renderer);

        // RUNTIME-086 Slices B/C — drives the deferred-retire window of the
        // runtime-owned graph-residency retire queue, mirroring
        // `TickMeshGeometry`. Handles enqueued by entity destruction,
        // eligibility flip, or full dirty reupload are freed via
        // `GpuWorld::FreeGeometry` once `framesInFlight` ticks have elapsed
        // since the release tick. Subsequent `ExtractAndSubmit` calls surface
        // the per-tick delta as `GraphGeometryFreeRetires`.
        void TickGraphGeometry(std::uint64_t currentFrame,
                               std::uint32_t framesInFlight,
                               Graphics::IRenderer& renderer);

        // RUNTIME-087 — drives the deferred-retire window of the runtime-owned
        // point-cloud-residency retire queue, mirroring `TickGraphGeometry`.
        // Handles enqueued by entity destruction, eligibility flip, or full
        // dirty reupload are freed via `GpuWorld::FreeGeometry` once
        // `framesInFlight` ticks have elapsed since the release tick. Subsequent
        // `ExtractAndSubmit` calls surface the per-tick delta as
        // `PointCloudGeometryFreeRetires`.
        void TickPointCloudGeometry(std::uint64_t currentFrame,
                                    std::uint32_t framesInFlight,
                                    Graphics::IRenderer& renderer);

        // RUNTIME-088 Slice B — drives the deferred-retire window of the
        // runtime-owned mesh-primitive-view residency retire queue, mirroring
        // `TickMeshGeometry`. Edge and vertex view geometry handles enqueued by
        // view disable, parent eligibility flip, dirty reupload, entity
        // destruction, or shutdown are freed via `GpuWorld::FreeGeometry` once
        // `framesInFlight` ticks have elapsed. Subsequent `ExtractAndSubmit`
        // calls surface the per-tick delta as `MeshPrimitiveViewFreeRetires`.
        void TickMeshPrimitiveViewGeometry(std::uint64_t currentFrame,
                                           std::uint32_t framesInFlight,
                                           Graphics::IRenderer& renderer);

        // ASSETIO-007 — data-only texture binding surface for renderables
        // whose material sidecar is owned by extraction. Callers key bindings
        // by stable render id; extraction resolves the AssetIds through the
        // provided GpuAssetCache when the renderable is submitted. No ECS
        // component stores graphics-owned material slots or handles.
        void SetMaterialTextureAssetBindings(
            std::uint32_t stableEntityId,
            Graphics::MaterialTextureAssetBindings bindings);
        void ClearMaterialTextureAssetBindings(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<Graphics::MaterialTextureAssetBindings>
            GetMaterialTextureAssetBindings(std::uint32_t stableEntityId) const noexcept;

        [[nodiscard]] const RuntimeRenderExtractionStats& GetLastStats() const noexcept;
        [[nodiscard]] std::uint32_t GetTrackedRenderableCount() const noexcept;
        [[nodiscard]] std::size_t GetLiveRenderableKeyScratchBucketCountForTest() const noexcept;

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
            // RUNTIME-086 Slice B — runtime-authored graph-source residency.
            // `GraphGeometry` is the single handle the cache owns and frees on
            // retirement for a graph-domain entity (node positions as the
            // shared vertex buffer, optional edge line indices); distinct from
            // `MeshGeometry` so the two domain bridges never alias.
            Graphics::GpuGeometryHandle GraphGeometry{};
            bool HasGraphResidency = false;
            Graphics::GpuInstanceHandle GraphPointLaneInstance{};
            bool HasGraphPointLaneInstance = false;
            // RUNTIME-087 — runtime-authored point-cloud-source residency. The
            // single handle the cache owns and frees on retirement for a
            // point-cloud-domain entity (point positions as the vertex buffer);
            // distinct from `MeshGeometry`/`GraphGeometry` so the three domain
            // bridges never alias.
            Graphics::GpuGeometryHandle PointCloudGeometry{};
            bool HasPointCloudResidency = false;
            // RUNTIME-088 Slice B — optional mesh primitive view sidecars.
            // Each enabled view owns a *separate* `GpuWorld` instance + geometry
            // handle (distinct from the parent surface instance/geometry above)
            // so faces, edges, and vertices render as independent lanes over the
            // one authoritative mesh data source.
            Graphics::GpuInstanceHandle MeshEdgeViewInstance{};
            Graphics::GpuGeometryHandle MeshEdgeViewGeometry{};
            bool HasMeshEdgeView = false;
            Graphics::GpuInstanceHandle MeshVertexViewInstance{};
            Graphics::GpuGeometryHandle MeshVertexViewGeometry{};
            bool HasMeshVertexView = false;
            Graphics::MaterialHandle MaterialHandle{};
            std::uint32_t MaterialSlot = Graphics::kDefaultMaterialSlotIndex;
            bool HasMaterialLease = false;
        };

        [[nodiscard]] std::optional<RenderableSidecarView> FindRenderableSidecarForTest(
            std::uint32_t stableEntityId) const noexcept;

        struct GpuRenderLaneAvailability
        {
            GeometryRenderLane Lane{GeometryRenderLane::Surface};
            bool HasInstance{false};
            bool HasGeometry{false};
            Graphics::GpuInstanceHandle Instance{};
            Graphics::GpuGeometryHandle Geometry{};
        };

        struct GpuRenderableAvailabilityView
        {
            std::uint32_t StableEntityId{0};
            bool HasRenderable{false};
            bool HasSourceAsset{false};
            std::uint32_t GeometrySlot{0};
            std::uint32_t GeometryGeneration{0};
            std::uint32_t NamedBufferCount{0};
            bool HasPositionsBuffer{false};
            bool HasNormalsBuffer{false};
            bool HasEdgesBuffer{false};
            bool HasColorsBuffer{false};
            bool HasScalarsBuffer{false};
            bool HasSizesBuffer{false};
            GpuRenderLaneAvailability Surface{};
            GpuRenderLaneAvailability Edges{};
            GpuRenderLaneAvailability Points{};
        };

        [[nodiscard]] std::optional<GpuRenderableAvailabilityView>
            FindGpuRenderableAvailability(std::uint32_t stableEntityId) const noexcept;

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

        enum class VisualizationAdapterBindingKind : std::uint8_t
        {
            Scalar,
            Color,
            VectorField,
            Isoline,
            HtexMetadata,
        };

        struct VisualizationAdapterBinding
        {
            std::uint64_t AdapterKey{0u};
            std::uint64_t BufferBDA{0u};
            VisualizationAdapterBindingKind Kind{
                VisualizationAdapterBindingKind::Scalar};
            VisualizationAdapterOptions Options{};
        };

        // RUNTIME-083 Slices B/E — runtime-owned visualization adapter binding
        // surface. Scalar/color/isolines may derive source metadata from
        // `VisualizationConfig`; vector fields and Htex metadata are supplied
        // through `Options`. Bindings provide the active adapter key and any
        // externally owned GPU buffer addresses the adapter must reference.
        // They are keyed by the same stable renderable id that extraction
        // sidecars and selection use.
        void RegisterVisualizationAdapter(std::uint64_t key,
                                          std::unique_ptr<IVisualizationAdapter> adapter);
        bool UnregisterVisualizationAdapter(std::uint64_t key) noexcept;
        [[nodiscard]] std::size_t GetVisualizationAdapterCount() const noexcept;
        [[nodiscard]] const VisualizationAdapterRegistry& GetVisualizationAdapterRegistryForTest() const noexcept;
        void SetVisualizationAdapterBinding(std::uint32_t stableEntityId,
                                            VisualizationAdapterBinding binding);
        void ClearVisualizationAdapterBinding(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<VisualizationAdapterBinding> GetVisualizationAdapterBinding(
            std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::uint64_t GetVisualizationAdapterBindingRevision() const noexcept;

    private:
        struct State;
        std::unique_ptr<State> m_State;

    };
}

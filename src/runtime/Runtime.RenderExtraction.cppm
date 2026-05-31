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
import Extrinsic.Runtime.GraphGeometryPacker;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.PointCloudGeometryPacker;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;
import Extrinsic.Runtime.SelectionController;
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

        // RUNTIME-086 Slices B/C — runtime-authored graph `GeometrySources`
        // residency counters, mirroring the mesh accounting above. A graph
        // entity carrying `RenderLines` and/or `RenderPoints` packs its node
        // positions (shared vertex buffer) plus optional `(e:v0, e:v1)` line
        // indices into one `GpuGeometryHandle`. `Uploads` is incremented once
        // per entity on the first frame the graph is packed and uploaded;
        // clean frames hit `ReuseHits`; dirty frames hit `Reuploads` after the
        // graph dirty-domain tags (`DirtyVertexPositions`,
        // `DirtyVertexAttributes`, `DirtyEdgeTopology`, `GpuDirty`) are drained
        // and the graph is repacked. `MissingNodes` aggregates the two
        // node-shape pack rejections (`MissingNodes`, `EmptyGraph`) and
        // `InvalidEdges` the out-of-range edge endpoint rejection because those
        // are the likeliest structural authoring bugs in graph sources; every
        // other non-`Success` status (`WrongDomain`, `NoRenderLane`,
        // `MissingEdgeTopology`, `NonFinitePosition`) folds into `FailedPack`.
        // `Releases` is incremented per release-initiated event (entity
        // destruction, eligibility flip away from graph, or dirty reupload
        // superseding an older handle); the actual free runs through the
        // `framesInFlight` deferred-retire window driven by `TickGraphGeometry`,
        // which surfaces `FreeRetires` as a per-frame delta on the next
        // `ExtractAndSubmit` (mirroring `MeshGeometryFreeRetires`).
        std::uint32_t GraphGeometryUploads{0};
        std::uint32_t GraphGeometryReuseHits{0};
        std::uint32_t GraphGeometryReuploads{0};
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
        // dirty frames hit `Reuploads` after the cloud dirty-domain tags
        // (`DirtyVertexPositions`, `DirtyVertexAttributes`, `GpuDirty`) are
        // drained and the cloud is repacked. `MissingPositions` aggregates the
        // two position-shape pack rejections (`MissingPositions`, `EmptyCloud`)
        // and `InvalidPoints` the non-finite-position rejection because those
        // are the likeliest structural authoring bugs in cloud sources; every
        // other bind-level rejection (`WrongDomain`, plus an unsupported
        // per-point `RenderPoints::SizeSource` buffer variant — only a uniform
        // float radius is supported in this slice) folds into `FailedPack`.
        // `Releases` is incremented per release-initiated event (entity
        // destruction, eligibility flip away from point-cloud, or dirty
        // reupload superseding an older handle); the actual free runs through
        // the `framesInFlight` deferred-retire window driven by
        // `TickPointCloudGeometry`, which surfaces `FreeRetires` as a per-frame
        // delta on the next `ExtractAndSubmit` (mirroring
        // `GraphGeometryFreeRetires`).
        std::uint32_t PointCloudGeometryUploads{0};
        std::uint32_t PointCloudGeometryReuseHits{0};
        std::uint32_t PointCloudGeometryReuploads{0};
        std::uint32_t PointCloudGeometryFailedPack{0};
        std::uint32_t PointCloudGeometryMissingPositions{0};
        std::uint32_t PointCloudGeometryInvalidPoints{0};
        std::uint32_t PointCloudGeometryReleases{0};
        std::uint32_t PointCloudGeometryFreeRetires{0};

        // RUNTIME-088 Slice B — runtime-owned mesh *primitive view* residency
        // counters. A mesh entity that opts into edge and/or vertex views (via
        // the cache-owned `MeshPrimitiveViewSettings`, set by runtime/editor
        // state — the flags never live in ECS components) derives one extra
        // retained renderable per enabled view from the *same* authoritative
        // mesh `GeometrySources`: the edge view binds a line-list
        // (`GpuRender_Line`) and the vertex view a point list
        // (`GpuRender_Point`), each through its own `GpuWorld` instance +
        // `GpuGeometryHandle` recorded in the parent mesh's sidecar. Each view
        // is a single-owner residency stream mirroring the surface-mesh
        // accounting: `Uploads` once on the frame a view is first created,
        // `ReuseHits` on clean frames, `Reuploads` when the parent mesh is
        // dirty (every mesh dirty domain is coalesced — views repack whenever
        // the surface repacks), `Releases` per release-initiated event (view
        // disabled, parent flips away from a resident mesh, dirty reupload
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

        // `selection`, when non-null, supplies the runtime-owned selection
        // snapshot (RUNTIME-089 Slice B): its `SelectedStableIds()` /
        // `HoveredStableId()` / `HasHovered()` are attached to the submitted
        // `RuntimeRenderSnapshotBatch` so the renderer surfaces them on
        // `RenderWorld::Selection`. Null leaves the selection snapshot empty.
        [[nodiscard]] RuntimeRenderExtractionStats ExtractAndSubmit(ECS::Scene::Registry& scene,
                                                                    Graphics::IRenderer& renderer,
                                                                    Graphics::GpuAssetCache* gpuAssets = nullptr,
                                                                    const SelectionController* selection = nullptr);
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

        // RUNTIME-086 Slices B/C — drives the deferred-retire window of the
        // runtime-owned graph-residency retire queue, mirroring
        // `TickMeshGeometry`. Handles enqueued by entity destruction,
        // eligibility flip, or dirty reupload are freed via
        // `GpuWorld::FreeGeometry` once `framesInFlight` ticks have elapsed
        // since the release tick. Subsequent `ExtractAndSubmit` calls surface
        // the per-tick delta as `GraphGeometryFreeRetires`.
        void TickGraphGeometry(std::uint64_t currentFrame,
                               std::uint32_t framesInFlight,
                               Graphics::IRenderer& renderer);

        // RUNTIME-087 — drives the deferred-retire window of the runtime-owned
        // point-cloud-residency retire queue, mirroring `TickGraphGeometry`.
        // Handles enqueued by entity destruction, eligibility flip, or dirty
        // reupload are freed via `GpuWorld::FreeGeometry` once `framesInFlight`
        // ticks have elapsed since the release tick. Subsequent
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

        // RUNTIME-088 Slice B — runtime/editor control surface for optional
        // mesh edge/vertex primitive views. The settings live here (cache-owned
        // runtime state), never in ECS components and never carrying graphics
        // handles; `ExtractAndSubmit` consults them when a mesh entity is
        // resident. `Set` upserts, `Clear` removes (disabling both views on the
        // next extraction), and `Get` returns the stored settings or a
        // both-disabled default when none are recorded. Keyed by the same
        // stable entity id the renderable sidecars use.
        void SetMeshPrimitiveViewSettings(std::uint32_t stableEntityId,
                                          MeshPrimitiveViewSettings settings);
        void ClearMeshPrimitiveViewSettings(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] MeshPrimitiveViewSettings GetMeshPrimitiveViewSettings(
            std::uint32_t stableEntityId) const noexcept;

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
            // RUNTIME-086 Slice B — runtime-authored graph-source residency.
            // `GraphGeometry` is the single handle the cache owns and frees on
            // retirement for a graph-domain entity (node positions as the
            // shared vertex buffer, optional edge line indices); distinct from
            // `MeshGeometry` so the two domain bridges never alias.
            Graphics::GpuGeometryHandle GraphGeometry{};
            bool HasGraphResidency = false;
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
            // RUNTIME-086 Slice B — owned graph-source residency handle.
            // Mesh and graph domains are mutually exclusive per entity, but
            // the handles are tracked separately so an entity that flips
            // domain releases the stale handle while the other path uploads.
            Graphics::GpuGeometryHandle GraphGeometry{};
            // RUNTIME-086 — the render-lane hints (`RenderLines` /
            // `RenderPoints`) the resident `GraphGeometry` upload was packed
            // for. The line lane's presence changes the packed upload (line
            // indices), so a change in requested lanes must force a repack even
            // when no geometry dirty tag is set — otherwise a points-only graph
            // that later gains `RenderLines` would rebind a lineless upload and
            // draw no lines until an unrelated dirty tag forced a repack.
            bool GraphPackedLines{false};
            bool GraphPackedPoints{false};
            // RUNTIME-087 Slice B — owned point-cloud-source residency handle.
            // Mesh, graph, and point-cloud domains are mutually exclusive per
            // entity, but the handles are tracked separately so an entity that
            // flips domain releases the stale handle while the other path
            // uploads. Point clouds carry no lane mask (positions only), so no
            // packed-lane tracking is needed.
            Graphics::GpuGeometryHandle PointCloudGeometry{};
            // RUNTIME-088 Slice B — optional mesh edge/vertex primitive view
            // sidecars attached to this mesh renderable. Each enabled view owns
            // its own `GpuWorld` instance (rendered as an extra line/point lane
            // with the same transform/bounds as the parent surface) plus a
            // single-owner geometry handle the cache frees on retirement.
            // Mutually independent of the surface `MeshGeometry`; both views are
            // released whenever the parent stops being a resident mesh.
            Graphics::GpuInstanceHandle MeshEdgeViewInstance{};
            Graphics::GpuGeometryHandle MeshEdgeViewGeometry{};
            Graphics::GpuInstanceHandle MeshVertexViewInstance{};
            Graphics::GpuGeometryHandle MeshVertexViewGeometry{};
        };

        // RUNTIME-088 Slice B — selects which primitive view a reconcile/release
        // call operates on. The lifecycle is identical for both; the kind only
        // changes the pack function, the render-lane flag, the sidecar
        // instance/geometry fields, and which counters move.
        enum class MeshPrimitiveViewKind : std::uint8_t
        {
            Edge,
            Vertex,
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
        [[nodiscard]] bool BindGraphGeometry(entt::registry& registry,
                                             entt::entity entity,
                                             const ECS::Components::GeometrySources::ConstSourceView& view,
                                             RenderableSidecar& sidecar,
                                             Graphics::IRenderer& renderer,
                                             RuntimeRenderExtractionStats& stats);
        [[nodiscard]] bool BindPointCloudGeometry(entt::registry& registry,
                                                  entt::entity entity,
                                                  const ECS::Components::GeometrySources::ConstSourceView& view,
                                                  RenderableSidecar& sidecar,
                                                  Graphics::IRenderer& renderer,
                                                  RuntimeRenderExtractionStats& stats);

        // RUNTIME-088 Slice B — reconcile one mesh primitive view against its
        // desired state for the frame. `desired` already folds the parent's
        // residency and the per-entity `MeshPrimitiveViewSettings` flag; when
        // true the view is created/reused/repacked (driven by `meshDirty`) and
        // a `TransformSyncRecord` is appended so it renders as an extra
        // line/point lane, and when false any existing view is released. `view`
        // must resolve `Domain::Mesh` (the caller only invokes this for a
        // resident mesh). `model`/`bounds`/`materialSlot` mirror the parent
        // surface so the view tracks the same transform.
        void ReconcileMeshPrimitiveView(MeshPrimitiveViewKind kind,
                                        const ECS::Components::GeometrySources::ConstSourceView& view,
                                        RenderableSidecar& sidecar,
                                        const glm::mat4& model,
                                        std::uint32_t materialSlot,
                                        const RHI::GpuBounds& bounds,
                                        std::uint32_t stableId,
                                        bool desired,
                                        bool meshDirty,
                                        Graphics::IRenderer& renderer,
                                        RuntimeRenderExtractionStats& stats);

        // RUNTIME-088 Slice B — release one mesh primitive view sidecar: enqueue
        // its geometry handle for deferred retire, free its instance, reset the
        // sidecar fields, and (when a handle was live) bump the view's
        // `Releases`. No-op when the view is not resident.
        void ReleaseMeshPrimitiveView(MeshPrimitiveViewKind kind,
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
        // A record is enqueued with `DeadlineSet = false`; the next
        // `TickMeshGeometry` / `TickGraphGeometry` anchors
        // `Deadline = currentFrame + framesInFlight` and frees the handle once
        // `Deadline <= currentFrame`. The record is domain-agnostic (handle +
        // deadline only), so the graph-residency retire queue reuses it.
        struct GeometryRetireRecord
        {
            Graphics::GpuGeometryHandle Handle{};
            std::uint64_t Deadline = 0;
            bool DeadlineSet = false;
        };

        void EnqueueMeshRetire(Graphics::GpuGeometryHandle handle);
        // RUNTIME-086 Slice B — graph-residency retire enqueue, mirroring
        // `EnqueueMeshRetire`.
        void EnqueueGraphRetire(Graphics::GpuGeometryHandle handle);
        // RUNTIME-087 — point-cloud-residency retire enqueue, mirroring
        // `EnqueueGraphRetire`.
        void EnqueuePointCloudRetire(Graphics::GpuGeometryHandle handle);
        // RUNTIME-088 Slice B — mesh-primitive-view retire enqueue. Edge and
        // vertex view geometry share one queue (both are mesh-domain residency
        // freed on the same `framesInFlight` window), mirroring the per-domain
        // enqueues above.
        void EnqueueMeshPrimitiveViewRetire(Graphics::GpuGeometryHandle handle);

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
        std::vector<GeometryRetireRecord> m_MeshRetire{};
        std::uint32_t m_MeshFreeRetires{0};
        std::uint32_t m_PrevMeshFreeRetires{0};

        // RUNTIME-086 Slices B/C — graph-residency scratch buffer + deferred-
        // retire queue, mirroring the mesh-residency members above.
        GraphPackBuffer m_GraphPack{};
        std::vector<GeometryRetireRecord> m_GraphRetire{};
        std::uint32_t m_GraphFreeRetires{0};
        std::uint32_t m_PrevGraphFreeRetires{0};

        // RUNTIME-087 — point-cloud-residency scratch buffer + deferred-retire
        // queue, mirroring the graph-residency members above.
        PointCloudPackBuffer m_PointCloudPack{};
        std::vector<GeometryRetireRecord> m_PointCloudRetire{};
        std::uint32_t m_PointCloudFreeRetires{0};
        std::uint32_t m_PrevPointCloudFreeRetires{0};

        // RUNTIME-088 Slice B — mesh-primitive-view scratch buffer (reused
        // serially across the edge then vertex pack each frame), shared
        // deferred-retire queue for both view lanes, and the FreeRetires
        // accumulator/prev-snapshot the per-tick delta is derived from.
        // `m_MeshPrimitiveViewSettings` is the cache-owned runtime/editor
        // control surface keyed by stable entity id.
        MeshPrimitiveViewBuffer m_MeshPrimitiveViewPack{};
        std::vector<GeometryRetireRecord> m_MeshPrimitiveViewRetire{};
        std::uint32_t m_MeshPrimitiveViewFreeRetires{0};
        std::uint32_t m_PrevMeshPrimitiveViewFreeRetires{0};
        std::unordered_map<std::uint32_t, MeshPrimitiveViewSettings> m_MeshPrimitiveViewSettings{};

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

# Runtime

`src/runtime` is the composition root for the engine. It owns
subsystem instantiation order, frame-phase orchestration, and deterministic
startup/shutdown.

## Public module surface

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.Engine` | Composition root, frame loop, subsystem wiring, app-facing reference engine config helper |
| `Extrinsic.Runtime.EcsSystemBundle` | Runtime-owned activation helper for the promoted baseline ECS systems. Exports `PromotedEcsSystemBundleStats` and `RegisterPromotedEcsSystemBundle(FrameGraph&, ECS::Scene::Registry&)`, which adds `Extrinsic.ECS.System.TransformHierarchy` and `Extrinsic.ECS.System.BoundsPropagation` as FrameGraph passes. `Engine::RunFrame()` invokes the helper inside the fixed-step substep loop after `IApplication::OnSimTick` and before `Core::FrameGraph::Compile`, so dirty world matrices and bounds are refreshed every substep before render extraction observes them (`RUNTIME-091`). |
| `Extrinsic.Runtime.CameraControllers` | Runtime-owned camera controller surface. Exports `ICameraController`, `OrbitCameraController`, `FlyCameraController`, `FreeLookCameraController`, `TopDownCameraController`, `CreateCameraController()`, `CameraControllerSlot`, and `CameraControllerRegistry`. Controllers consume `Extrinsic.Platform.Input::Context`, use `Core::Extent2D` for viewport dimensions, and produce immutable `Graphics::CameraViewInput` for renderer extraction. The registry exposes named slots (`Main`, `Preview`, `TopDown`, `EditorSecondary`) while `Engine::RunFrame()` currently drives the `Main` slot. |
| `Extrinsic.Runtime.ProceduralGeometry` | Procedural-geometry descriptor surface (`ProceduralGeometryKey`, key hash, `ProceduralGeometryCache` value type with `EnsureResident` / `Release` / `Tick` / `Find`). Reuses the `ProceduralGeometryKind` enum and POD `ProceduralGeometryParams` defined in `Extrinsic.ECS.Component.ProceduralGeometryRef`. `EnsureResident(key, uploadDesc, uploadFn)` either invokes the injected upload functor exactly once on a new key or hits an existing entry and increments a `std::uint32_t` refcount; `Release(key)` decrements and enqueues the entry into a deferred retire queue on the refcount-zero transition; `Tick(currentFrame, framesInFlight, freeFn)` anchors retire deadlines (`currentFrame + framesInFlight`) and calls `freeFn` on entries whose deadline has been reached, mirroring `Graphics::GpuAssetCache::Tick` semantics. Resurrecting a key inside the retire window cancels the queued free and reuses the bit-identical `GpuGeometryHandle`. N entities sharing `(Kind, Hash(Params))` share one `GpuGeometryHandle`. No live ECS, no graphics imports beyond the existing `Extrinsic.Graphics.GpuWorld` value-type edge. |
| `Extrinsic.Runtime.ProceduralGeometryPacker` | Per-kind packer `Pack(kind, params, scratch) -> std::optional<GeometryUploadDesc>` consuming a runtime-owned `ProceduralGeometryPackBuffer` reused across ticks. Triangle is the only in-scope packer for Impl-A; the vertex layout is `{pos.xyz, uv}` (20 bytes/vertex) matching `Test.MinimalTriangleAcceptance`. Cube / Quad / Sphere / LineStrip extend the enum + packer table without cache or extraction lifecycle changes. |
| `Extrinsic.Runtime.ReferenceScene` | Opt-in runtime-owned reference scene seam (GRAPHICS-029A/B). Exports `IReferenceSceneProvider`, `ReferenceSceneRegistry`, `ReferenceSceneEntity`/`ReferenceScenePopulation`, `TriangleProvider`, `MakeDefaultReferenceSceneRegistry()`, `RegisterDefaultReferenceProvidersIfAbsent()`, and `BuildReferenceCameraViewInput()`. `Engine::Initialize()` invokes `RegisterDefaultReferenceProvidersIfAbsent` so any unregistered selector receives its production default (currently `TriangleProvider` for `Triangle`), then resolves `EngineConfig::ReferenceScene::Selector` against `Engine::GetReferenceSceneRegistry()` exactly once after scene-registry construction and before `IApplication::OnInitialize`. The returned `ReferenceScenePopulation` is stored so `Engine::Shutdown()` routes teardown through the same provider before the scene registry is destroyed; the optional `CameraViewInput` seed is captured on `m_ReferenceCamera` and consumed as the initial state for `Extrinsic.Runtime.CameraControllers`. `m_ReferenceSceneInstalled` guards against double-install via `std::terminate`, and `ReferenceSceneRegistry::Resolve()` itself terminates on unregistered selectors (GRAPHICS-029 Decision 7 applied to both register and resolve). `TriangleProvider::Populate` calls `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")`, attaches `Graphics::Components::RenderSurface{Domain = Vertex}` and `ECS::Components::ProceduralGeometryRef{Kind = Triangle}`, and returns a CameraViewInput seed (position (0,0,3), forward (0,0,-1), up (0,1,0), near 0.1, far 100). |
| `Extrinsic.Runtime.RenderExtraction` | Runtime-owned ECS-to-graphics extraction cache and snapshot handoff |
| `Extrinsic.Runtime.SpatialDebugAdapters` | Runtime-only translation seam from geometry-tree implementations (`Geometry.BVH`, `Geometry.KDTree`, `Geometry.Octree`, `Geometry.ConvexHull`) into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers` packet types (clarified by `GRAPHICS-011Q`; tracked by `RUNTIME-082`). Exports `SpatialDebugSnapshotBatch` (mutable per-frame output container with `Bounds`, `HierarchyNodes`, `SplitPlanes`, `ConvexHullVertices`, `ConvexHullEdges`, `PointMarkers` spans plus a `Clear()` helper), `SpatialDebugAdapterOptions` (`LeafOnly`, `OccupancyOnly`, `MaxDepth`), `SpatialDebugAdapterStats` (per-`Append` accumulator with `LeafNodeCount`, `InnerNodeCount`, `SplitPlaneCount`, `EmptyNodeSkippedCount`, `DepthCapTruncationCount`), the pure-virtual `ISpatialDebugAdapter` interface, the `BvhAdapter` (Slice A), `KdTreeAdapter`, `OctreeAdapter` (Slice B), `ConvexHullAdapter` (Slice C) concretes, and the `SpatialDebugAdapterRegistry` (Slice C) key→adapter table. Runtime is the only layer permitted to import both geometry tree implementations and the graphics packet types per `AGENTS.md` §2. `KdTreeAdapter` mirrors the BVH pattern (one `SpatialDebugSplitPlane` per inner node carrying `node.SplitAxis`/`node.SplitValue`); `OctreeAdapter` emits three perpendicular `SpatialDebugSplitPlane`s per inner node — one per axis at the parent AABB center — because `Geometry::Octree::Node` does not record the chosen split point. The center-based visualization is exact for `SplitPoint::Center` and an explicit approximation for `Mean`/`Median`, which the adapter does not attempt to reconstruct. `ConvexHullAdapter` copies the hull's V-Rep into `ConvexHullVertices` and derives `ConvexHullEdges` by plane-incidence (two vertices form a `SpatialDebugWireEdge` when they share ≥2 face planes within `IncidenceEpsilon`); it ignores `LeafOnly`/`OccupancyOnly`/`MaxDepth` and leaves `SpatialDebugAdapterStats` untouched because none of the tree-shaped concepts apply to a flat hull. `SpatialDebugAdapterRegistry` maps an opaque `std::uint64_t` renderable key onto a non-owning `const ISpatialDebugAdapter*`; callers own adapter lifetime and must `Unregister` before the adapter or its source geometry tree is destroyed. `RUNTIME-082` Slice A scaffolded the umbrella + BvhAdapter + value types; Slice B added the KdTree/Octree adapters; Slice C landed the ConvexHull adapter + registry; Slice D wires the pump through `RenderExtractionCache::ExtractAndSubmit` (`RegisterSpatialDebugAdapter` / `UnregisterSpatialDebugAdapter` transfer `std::unique_ptr<ISpatialDebugAdapter>` ownership into the cache and mirror into an embedded `SpatialDebugAdapterRegistry`; the extraction loop walks the `ECS::Components::SpatialDebugBinding` view, accumulates a shared `SpatialDebugSnapshotBatch`, attaches its spans to `RuntimeRenderSnapshotBatch::SpatialDebug{Bounds,HierarchyNodes,SplitPlanes,ConvexHullVertices,ConvexHullEdges,PointMarkers}`, and folds per-frame counters onto `RuntimeRenderExtractionStats`'s `SpatialDebug{BindingsObserved,AdaptersInvoked,MissingAdapterCount,BoundsCount,HierarchyNodeCount,SplitPlaneCount,ConvexHullVertexCount,ConvexHullEdgeCount,PointMarkerCount,LeafNodeAccumulator,InnerNodeAccumulator,EmptyNodeSkippedAccumulator,DepthCapTruncationAccumulator}` fields). |
| `Extrinsic.Runtime.StreamingExecutor` | Persistent background streaming task execution |

`Extrinsic.Runtime.Engine` exports `CreateReferenceEngineConfig()` so reference
applications can request the standard runtime configuration without importing
lower-layer `core` config modules directly. Applications may pass the returned
config to `Engine`; runtime remains responsible for interpreting subsystem
configuration and composition. `CreateReferenceEngineConfig()` flips
`EngineConfig::ReferenceScene::Enabled = true` and
`Selector = ReferenceSceneSelector::Triangle`; the default-constructed
`EngineConfig{}` keeps `Enabled = false` so existing CPU/null tests do not
regress. `CreateReferenceEngineConfig()` also sets
`Render.EnablePromotedVulkanDevice = true` so the reference sandbox requests the
promoted Vulkan backend by default (GRAPHICS-080). Whether the runtime actually
binds Vulkan or falls back to Null is governed by the GRAPHICS-033 truth table
in `src/graphics/vulkan/README.md` — when the promoted backend was not compiled
in, the host lacks Vulkan support, or the operational gate is not yet
satisfied, the runtime resolves to Null, emits the
`VulkanRequestedButNotOperational` breadcrumb, and continues. The
default-constructed `EngineConfig{}` keeps
`Render.EnablePromotedVulkanDevice = false` so unit/contract suites that drive
`Engine::Initialize()` against the Null device do not regress and do not emit
the breadcrumb.

Runtime consumes `Extrinsic.Core.FrameLoop` for reusable platform/render/
maintenance/shutdown phase contracts. The contract lives in `core` because it has
no higher-layer imports; `Runtime.Engine` supplies runtime-specific hook
implementations during composition.

## Engine initialisation ordering

`Engine::Initialize()` runs the following ordered steps once per engine
lifetime (re-`Initialize` after `Shutdown` repeats the same order against
freshly-constructed subsystems):

1. `Core::Tasks::Scheduler::Initialize` — CPU fiber scheduler must be live
   before any task-graph or streaming dispatch runs.
2. Platform window, RHI device, renderer construction + `Initialize`.
   Immediately after `IDevice::Initialize`, the runtime evaluates
   `ShouldEmitVulkanRequestedButNotOperationalBreadcrumb(...)` against the
   resolved `RenderConfig` and `IDevice::IsOperational()`. When the runtime
   requested the promoted Vulkan device (`Backend == Vulkan` &&
   `EnablePromotedVulkanDevice`) but the resolved device is non-operational,
   `Core::Log::Warn` emits one
   `[Runtime] VulkanRequestedButNotOperational status={...} reason={...}`
   breadcrumb per `Engine::Initialize()` and calls
   `Backends::Vulkan::RecordVulkanOperationalFallback(...)` to advance the
   `VulkanOperationalDiagnosticsSnapshot` counters (GRAPHICS-033B). When the
   Vulkan backend is not compiled in, the status/reason text is
   `NotCompiled`/`None` and no Vulkan counters are touched (the diagnostics
   surface does not exist in that build). Runtime never aborts solely
   because requested Vulkan falls back to Null — see the truth table in
   `src/graphics/vulkan/README.md`.
3. CPU `FrameGraph` and streaming `TaskGraph` + `StreamingExecutor`.
4. `Assets::AssetService`.
5. `Graphics::GpuAssetCache` construction with the renderer's
   `BufferManager`, `TextureManager`, `SamplerManager`, and the device's
   `TransferQueue`. Immediately after construction the runtime calls
   `GpuAssetCache::InitializeFallbackTexture(...)` with the runtime-baked
   4×4 magenta-and-black `RGBA8_UNORM` checkerboard bytes and a
   nearest/clamp-to-edge sampler descriptor (RUNTIME-070). The call is
   gated on `m_Device->IsOperational()`; on the Null backend (or any
   non-operational device) the bootstrap is skipped and
   `GpuAssetCacheDiagnostics::FallbackTextureReady` stays `false`, leaving
   material code on its factor-only shading branch as documented in
   `src/graphics/assets/README.md`. A non-`Ok` result from
   `InitializeFallbackTexture` is logged as a single `Core::Log::Warn`
   breadcrumb and otherwise treated as a graceful "fallback unavailable"
   transition. The `AssetEventBus` listener is registered after the
   bootstrap attempt so a failed bootstrap does not block event
   subscription.
6. ECS `Scene::Registry` and the opt-in reference-scene bootstrap
   (GRAPHICS-029A/B). The optional camera seed is retained for the runtime
   camera controller created lazily on the first render-input snapshot.
7. `IApplication::OnInitialize`.

## Canonical frame loop phases (`Engine::RunFrame`)

1. Platform events / resize handling.
2. Fixed-step simulation. Each substep calls `IApplication::OnSimTick`, then
   `RegisterPromotedEcsSystemBundle` to append `TransformHierarchy` /
   `BoundsPropagation` to the CPU `FrameGraph`, and finally compiles and
   executes the graph so dirty world matrices and bounds are recomputed before
   the next substep or render extraction (`RUNTIME-091`).
3. Variable tick.
4. Render input snapshot.
5. Renderer begin frame.
6. Runtime render extraction: ECS queries, runtime sidecars, dirty-domain interpretation, deletion cleanup, and `IRenderer::SubmitRuntimeSnapshots()` handoff.
7. Renderer render-world extraction.
8. Render prepare.
9. Render execute.
10. End frame + present.
11. Maintenance: transfer retirement, streaming drain/apply/pump, asset service tick, `GpuAssetCache::Tick`, and `RenderExtractionCache::TickProceduralGeometry` (procedural geometry deferred-retire window).
12. Frame clock finalize.

`Engine::RunFrame()` consumes `Extrinsic.Core.FrameClock` for wall-clock frame
delta sampling and post-sleep resampling; runtime owns the phase orchestration,
not the reusable clock value type.

## Streaming integration

`Extrinsic.Runtime.StreamingExecutor` is the primary persistent async streaming
execution path. `Engine` still carries a temporary compatibility bridge for
legacy `GetStreamingGraph()` producers while migration is in progress.

Shutdown order requirement:

1. `StreamingExecutor::ShutdownAndDrain()`
2. task scheduler teardown

## Dependency direction

`Runtime` depends on `Core`, `Assets`, `ECS`, `Platform`, and `Graphics`.
It orchestrates but does not own GPU barrier/resource policy or render-pass
branching logic.

`Extrinsic.Runtime.RenderExtraction` is the only promoted runtime owner for live
ECS render queries. It stores entity-to-graphics sidecars outside canonical ECS
components, allocates/frees `GpuWorld` instance handles through the renderer,
builds transform/light/visualization/gizmo snapshot records, and submits those
records to graphics through `IRenderer::SubmitRuntimeSnapshots()`. It also owns
the `GRAPHICS-023C` asset-generation observation bridge: when a renderable has
`AssetInstance::Source`, extraction can compare the normalized `Assets::AssetId`
with a supplied `Graphics::GpuAssetCache` view and `GpuSceneSlot` metadata to
report pending/up-to-date/rebind-required states without performing the later
GPU geometry or material rebind.

`RenderExtractionCache` also owns the procedural-source residency bridge
(GRAPHICS-030B). When a renderable candidate carries
`ECS::Components::ProceduralGeometryRef`, `ExtractAndSubmit()` derives the
`ProceduralGeometryKey` from `(Kind, Hash(Params))`, drives
`ProceduralGeometryCache::EnsureResident(key, desc, upload)` against the
runtime-owned `ProceduralGeometryPackBuffer` scratch and `GpuWorld::UploadGeometry`,
and on success calls `GpuWorld::SetInstanceGeometry(instance, geometry)`,
records the resolved key in the sidecar, and applies the procedural-source
sentinel rule by calling `GpuSceneSlot::ClearSourceAsset()` (per GRAPHICS-030
Decision 5). The packer is only invoked on a cache miss; cache hits do
refcount-only work. If a renderable also carries a non-default
`AssetInstance::Source`, the procedural path is skipped for that entity and
`ProceduralAndAssetSourceConflict` is incremented while the existing asset
observation continues. Retired or shutdown sidecars call
`ProceduralGeometryCache::Release(key)`. When `Release` brings the refcount to
zero the entry is appended to an in-cache retire queue but the underlying
`GpuGeometryHandle` is **not** freed inline; it is freed by
`ProceduralGeometryCache::Tick(currentFrame, framesInFlight, freeFn)` after
`framesInFlight` ticks have elapsed since the release tick, mirroring the
`Graphics::GpuAssetCache::Tick` deferred-retire window (GRAPHICS-030C
Decision 4). `Engine::RunFrame()` drives the procedural cache from the
maintenance phase alongside `GpuAssetCache::Tick` via
`RenderExtractionCache::TickProceduralGeometry(currentFrame, framesInFlight,
renderer)`, which closes over `GpuWorld::FreeGeometry`. If an entity is
re-attached to the same `(Kind, Hash(Params))` inside the retire window,
`EnsureResident` resurrects the queued entry (cancelling the pending free),
returns the bit-identical `GpuGeometryHandle`, and increments
`ProceduralGeometryRetireCancellations`. Refcount saturation
(`UINT32_MAX`) is fail-closed: increments past the cap reject and bump
`ProceduralGeometryRefCountSaturated` instead of overflowing. The counter
fields on `RuntimeRenderExtractionStats` are
`ProceduralRenderablesEnumerated`, `ProceduralGeometryUploads`,
`ProceduralGeometryReuseHits`, `ProceduralGeometryFailedPack`,
`ProceduralGeometryMissingPacker`, `ProceduralGeometryInvalidParams`,
`ProceduralAndAssetSourceConflict`, `ProceduralAndRenderableSourceConflict`
(reserved for future asset-backed renderable conflict detection in
GRAPHICS-034), and the per-tick deltas of the cache's retire counters:
`ProceduralGeometryReleases`, `ProceduralGeometryFreeRetires`,
`ProceduralGeometryRetireCancellations`, and
`ProceduralGeometryRefCountSaturated`. `FindRenderableSidecarForTest(stableId)`
and `GetProceduralGeometryCacheForTest()` are read-only test seams used by the
`contract;runtime` procedural-geometry tests; `PrimeRefCountForTest` on the
cache is a test-only refcount setter that lets saturation coverage exercise
the rejection path without `2^32` `EnsureResident` calls.

Runtime owns camera motion, input-to-pick-request translation, gizmo hit testing,
and transform application. Graphics receives only immutable `CameraViewInput`,
`PickPixelRequest`, and transform-gizmo render packets during extraction.

## Camera controller baseline

`EngineConfig::Camera` selects the runtime-owned main camera controller. The
default is enabled `Orbit`; `Fly`, `FreeLook`, and `TopDown` are also
implemented and constructible through `CreateCameraController()`. `Engine::RunFrame()` lazily
registers the selected controller in `CameraControllerSlot::Main`, seeds it
from the reference scene when available, calls
`controller->Update(window.GetInput(), dt)`, and copies
`controller->GetView(viewport)` into `RenderFrameInput::Camera` before renderer
extraction. If the reference scene is disabled or returns no camera, the
controller falls back to a deterministic default perspective camera at
`(0, 0, 4)` looking down `-Z`.

The reference-scene seed is still finalized with `BuildReferenceCameraViewInput`
before seeding so its projection keeps the Vulkan clip-space Y inversion used by
the legacy `Graphics::CameraComponent` update at
`src/legacy/Graphics/Graphics.Camera.cpp:34-39`. After seeding, controller state
is authoritative and graphics receives only immutable `CameraViewInput`.

Known gaps relative to legacy and planned camera work are tracked in
`tasks/done/RUNTIME-081A-camera-legacy-gap-analysis.md`: editor-specific camera
shortcuts, transform-gizmo interaction, and any policy that renders multiple
camera outputs in one frame remain outside this runtime-controller surface.


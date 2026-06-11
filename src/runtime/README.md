# Runtime

`src/runtime` is the composition root for the engine. It owns
subsystem instantiation order, frame-phase orchestration, and deterministic
startup/shutdown.

## Public module surface

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.Engine` | Composition root, frame loop, subsystem wiring, app-facing reference engine config helper, the runtime-owned `ImportAssetFromPath(...)` facade that composes promoted ASSETIO geometry/model/texture decoders, `AssetService`, standalone geometry ECS materialization with local/world culling bounds plus one-shot main-camera focus for mesh/graph/point-cloud imports, and model/texture handoffs for editor file/import commands. Platform drop events enqueue geometry decode/conversion on `Runtime.StreamingExecutor`; `AssetService` and ECS materialization stay in the main-thread apply phase. Dropped ambiguous geometry extensions such as PLY try supported geometry payloads in import-router order before failing closed. Mesh imports that fail strict shared-topology conversion only for renderable non-manifold/inconsistent-winding diagnostics materialize through a disconnected render-only mesh fallback; geometry algorithms still use the strict converter as their topology contract. The `SaveSceneToPath(...)` / `LoadSceneFromPath(...)` / `NewSceneDocument()` / `CloseSceneDocument()` scene-file facades route through one runtime scene-replacement boundary: pre-clear render-extraction sidecar drain, selection hover/click/pick-correlation cleanup, refined-primitive cache reset, scene registry replacement/clear, stable-lookup rebuild or clear, and `EditorCommandHistory` dirty/path transitions. Scene-changing import facades mark the same runtime history dirty; UI reads the history snapshot but does not own document state. |
| `Extrinsic.Runtime.AssetGeometryIO` | Runtime-owned registration seam for `ASSETIO-001` Slice B. Exports `RegisterPromotedGeometryIOCallbacks(Assets::AssetGeometryIOBridge&)`, imports the promoted geometry IO modules in runtime, and registers OBJ/OFF/STL/PLY mesh importers, XYZ/PTS/XYZRGB/PCD/PLY point-cloud importers, TGF/edge-list graph importers, OBJ/STL/PLY mesh exporters, XYZ/PCD/PLY point-cloud exporters, and TGF/edge-list graph exporters. The adapter translates legacy geometry `Core.Error` decoder failures into promoted `Extrinsic.Core.Error` codes before they enter the asset bridge, keeping `src/assets` free of geometry/runtime/graphics imports. It does not construct ECS entities or GPU residency; later `ASSETIO-001` slices own model/texture payloads and runtime handoff. |
| `Extrinsic.Runtime.AssetModelTextureIO` | Runtime-owned registration seam for `ASSETIO-001` Slice C.2b. Exports `RegisterPromotedModelTextureIOCallbacks(Assets::AssetModelTextureIOBridge&)`, imports tinygltf/stb and promoted geometry IO privately in runtime, registers GLTF/GLB model-scene decoders and PNG/JPEG/TGA/BMP/HDR texture decoders, and maps decoded mesh primitives, embedded images, material texture references, and external-resource diagnostics into the promoted asset payload records. `src/assets` owns route resolution, primary/external byte transport, callback dispatch, and payload validation; runtime owns concrete decoder dependencies. Texture GPU residency flows through `Extrinsic.Runtime.AssetModelTextureHandoff`; model-scene ECS/material records flow through `Extrinsic.Runtime.AssetModelSceneHandoff`. |
| `Extrinsic.Runtime.SceneSerialization` | Backend-neutral scene document seam (`RUNTIME-098`, hardened by `RUNTIME-100`). Exports JSON save/load helpers over `ECS::Scene::Registry` plus `Core::IO::IIOBackend`, result/stat records, and fail-closed diagnostics. The current promoted document persists metadata names, durable stable ids, local transforms, hierarchy parent links, selectable tags, render geometry hints, visualization configs, and mesh/graph/point-cloud `GeometrySources` property data for sandbox-authored entities. Unsupported persistence families are counted deterministically in `SceneSerializationStats` (`Unsupported*Entities`) instead of being silently treated as supported. It deliberately omits renderer/RHI caches, GPU handles, dirty-tracker UX, file dialogs, arbitrary legacy asset source reimport, renderer/runtime visualization adapter bindings, and arbitrary component persistence. |
| `Extrinsic.Runtime.AssetModelTextureHandoff` | Runtime-owned texture residency seam for `ASSETIO-001` Slice D.1. Exports `BuildGpuTextureDesc(...)`, `RequestTextureAssetUpload(...)`, diagnostics/options records, and `AssetModelTextureHandoff`, which subscribes to `AssetService::Ready` events, reads promoted `AssetTexture2DPayload` records, maps supported CPU texture formats to RHI texture descriptors, and submits `Graphics::GpuAssetCache::RequestUpload(GpuTextureRequest)` without importing graphics/RHI into `src/assets`. RGB8 and unknown CPU texture formats fail closed as `AssetUnsupportedFormat` and can mark the GPU cache entry failed. `DeviceNotOperational` and in-flight upload conflicts are recorded as retryable upload deferrals so CPU imports can succeed under the Null backend. `RequestTextureAssetUpload(...)` is idempotent for cache entries already `GpuUploading` or `Ready`, so embedded child textures requested by the model-scene handoff do not double-submit when their own texture `Ready` event is later observed. |
| `Extrinsic.Runtime.AssetModelSceneHandoff` | Runtime-owned model-scene materialization seam for `ASSETIO-001` Slice D.2. Exports deterministic embedded-texture child-path helpers (`<model-path>.embedded-texture-<image-index>.<ext>`), `LoadEmbeddedTextureAsset(...)`, `MaterializeModelSceneAsset(...)`, diagnostics/record types, and `AssetModelSceneHandoff`, which subscribes to model-scene `AssetService::Ready` events. The handoff reads promoted `AssetModelScenePayload` records, mints child `AssetTexture2DPayload` assets keyed by stable synthetic paths, optionally requests their GPU uploads through `AssetModelTextureHandoff`, converts mesh primitive payloads into ECS `GeometrySources` mesh domains, creates default ECS entities with `RenderSurface`, and creates `MaterialSystem` leases plus `MaterialTextureAssetBindings` keyed by child `AssetId`s. Generated entities intentionally stay on the existing `GeometrySources` residency lane and do not stamp `AssetInstance::Source`, because the current extraction contract treats `AssetInstance::Source` as the alternative asset-cache observation path and suppresses `GeometrySources` mesh residency. Material records preserve the resolved material slots and `AssetId` texture bindings; bindings defer while child texture assets are pending or the headless GPU backend is non-operational, and `ResolvePendingMaterialTextureBindings()` re-resolves after upload or reload without rerunning model import. Broader file-backed GPU readback remains opt-in `Operational` work such as `RUNTIME-095` or a future value-gated task. |
| `Extrinsic.Runtime.EcsSystemBundle` | Runtime-owned activation helper for the promoted baseline ECS systems. Exports `PromotedEcsSystemBundleStats` and `RegisterPromotedEcsSystemBundle(FrameGraph&, ECS::Scene::Registry&)`, which adds `Extrinsic.ECS.System.TransformHierarchy` and `Extrinsic.ECS.System.BoundsPropagation` as FrameGraph passes. `Engine::RunFrame()` invokes the helper inside the fixed-step substep loop after `IApplication::OnSimTick` and before `Core::FrameGraph::Compile`, so dirty world matrices and bounds are refreshed every substep before render extraction observes them (`RUNTIME-091`). Also exports `PreRenderTransformFlushStats` and `FlushPreRenderTransformState(ECS::Scene::Registry&)` (BUG-024): a direct `TransformHierarchy` → `BoundsPropagation` → `RenderSync` pass run by `Engine::RunFrame()` after the variable tick, ImGui editor hook, and gizmo-interaction drive — and before transform-gizmo packet build and render extraction — so post-fixed-step UI/editor/gizmo transform edits reach the rendered model matrix in the same frame. |
| `Extrinsic.Runtime.EditorCommandHistory` | Runtime/editor-owned undo/redo and document dirty-state seam (`RUNTIME-102`). Exports `EditorCommandHistory`, deterministic result/status/snapshot DTOs, typed adapters for transform edits, single-selection replacement, mesh primitive-view settings, visualization configs, spatial-debug bindings, compound commands with rollback, and a hierarchy delete/orphan planning helper. The history stores labels, capacity-bounds undo/redo stacks, active scene path, revision/saved-revision dirty tracking, and fail-closed stale/missing dependency statuses. ECS remains data-authoritative; the service lives in runtime because editor command policy, sidecars, dirty-state UX, and recursive hierarchy policy are above ECS. Scene create/duplicate/delete materialization and asset-import undo stay deferred until runtime scene lifecycle/snapshot support can make them reversible without serializing renderer/RHI state. |
| `Extrinsic.Runtime.PhysicsBridge` | Runtime-owned ECS-to-physics bridge added by `PHYSICS-001`. Exports `PhysicsBridgeFixedStepConfig`, `PhysicsBridgeDiagnostics`, and `PhysicsBridge`, which owns an `Extrinsic.Physics.World`, a `StableId -> BodyHandle` sidecar, descriptor synchronization from ECS collider/rigid-body authoring, fixed-step accumulator stepping, and dynamic-body transform writeback with `Transform::IsDirtyTag` / `Transform::WorldUpdatedTag` stamping. The bridge keeps handles out of ECS, skips static/kinematic writeback with diagnostics, and deliberately does not route contact events until `PHYSICS-002` exposes contact records. |
| `Extrinsic.Runtime.CameraControllers` | Runtime-owned camera controller surface. Exports `CameraFocusTarget`, `ICameraController`, `OrbitCameraController`, `FlyCameraController`, `FreeLookCameraController`, `TopDownCameraController`, `CreateCameraController()`, `CameraControllerSlot`, and `CameraControllerRegistry`. `ICameraController::Focus(...)` performs one-shot centering/framing of imported or selected geometry without making UI own camera state. Controllers consume `Extrinsic.Platform.Input::Context`, use `Core::Extent2D` for viewport dimensions, and produce immutable `Graphics::CameraViewInput` for renderer extraction. `TopDownCameraController` seeds from the input view focus point, not the input position XZ, so the default reference triangle remains centered when starting in or switching to top-down mode. GRAPHICS-040A keeps the base `CameraViewInput` ABI stable; graphics-side temporal jitter is selected through `BuildTemporalCameraViewSnapshot(...)`, which accepts the rendered-frame index explicitly, while GRAPHICS-040C maps the renderer AA selector to TAA/external reconstruction without adding runtime camera authority. The registry exposes named slots (`Main`, `Preview`, `TopDown`, `EditorSecondary`) while `Engine::RunFrame()` currently drives the `Main` slot; UI-001 Slice C exposes the engine-owned registry through `Engine::GetCameraControllerRegistry()` so editor commands can replace slots without storing camera authority in UI state. |
| `Extrinsic.Runtime.ProceduralGeometry` | Procedural-geometry descriptor surface (`ProceduralGeometryKey`, key hash, `ProceduralGeometryCache` value type with `EnsureResident` / `Release` / `Tick` / `Find`). Reuses the `ProceduralGeometryKind` enum and POD `ProceduralGeometryParams` defined in `Extrinsic.ECS.Component.ProceduralGeometryRef`. `EnsureResident(key, uploadDesc, uploadFn)` either invokes the injected upload functor exactly once on a new key or hits an existing entry and increments a `std::uint32_t` refcount; `Release(key)` decrements and enqueues the entry into a deferred retire queue on the refcount-zero transition; `Tick(currentFrame, framesInFlight, freeFn)` anchors retire deadlines (`currentFrame + framesInFlight`) and calls `freeFn` on entries whose deadline has been reached, mirroring `Graphics::GpuAssetCache::Tick` semantics. Resurrecting a key inside the retire window cancels the queued free and reuses the bit-identical `GpuGeometryHandle`. N entities sharing `(Kind, Hash(Params))` share one `GpuGeometryHandle`. No live ECS, no graphics imports beyond the existing `Extrinsic.Graphics.GpuWorld` value-type edge. This procedural path remains available for explicit procedural fixtures and callers; the default reference/sandbox triangle now uses mesh-domain `GeometrySources`. |
| `Extrinsic.Runtime.ProceduralGeometryPacker` | Per-kind packer `Pack(kind, params, scratch) -> std::optional<GeometryUploadDesc>` consuming a runtime-owned `ProceduralGeometryPackBuffer` reused across ticks. Triangle is the only in-scope packer for Impl-A; the vertex layout is `{pos.xyz, uv}` (20 bytes/vertex) matching `Test.MinimalTriangleAcceptance`. Cube / Quad / Sphere / LineStrip extend the enum + packer table without cache or extraction lifecycle changes. |
| `Extrinsic.Runtime.MeshGeometryPacker` | Runtime-authored mesh `GeometrySources` → `GpuWorld::GeometryUploadDesc` packer (`RUNTIME-085` Slice A). Exports `MeshVertex` (20-byte `{pos.xyz, uv}` layout matching `ProceduralVertex`), `MeshPackBuffer` (caller-owned scratch reused across ticks), `MeshPackStatus` (`Success`, `WrongDomain`, `MissingPositions`, `MissingHalfedgeTopology`, `MissingFaceTopology`, `EmptyMesh`, `InvalidTopology`, `NonFinitePosition`, `DegenerateAllFaces`), `MeshPackResult`, `PackMesh(ConstSourceView, MeshPackBuffer&)`, and `DebugNameForMeshPackStatus()`. `PackMesh` validates `v:position`, `h:to_vertex`, `h:next`, `h:face`, and `f:halfedge`; for each face slot it consults `h:face` on the first ring halfedge to decide ownership and skips slots whose ring no longer claims the face — this is required because `ECS::Components::GeometrySources::PopulateFromMesh` writes `f:halfedge` for every face slot via `mesh.Halfedge(fh)`, but `Geometry::HalfedgeMesh::DeleteFace` invalidates only `h:face` on the ring's halfedges, leaving the deleted face's `f:halfedge` pointing at a still-walkable ring that must not be fan-triangulated. For live slots the ring is walked (step-capped at `halfedgeCount` so a malformed `h:next` cycle fails closed rather than spinning) and fan-triangulated from the first ring vertex; a mid-ring halfedge whose `h:face` disagrees with the current face fails closed as `InvalidTopology`. Vertex bytes are written in input order so emitted surface indices index directly into the source `Vertices` PropertySet; UV is zeroed (mesh-driven UV propagation is owned by a later slice). `LocalBounds.LocalSphere` is filled from the local AABB midpoint and half-diagonal so culling/transform sync has a deterministic non-empty local bound; `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites them with the per-frame world transform via `ExtractBounds`. Slice A delivers the standalone packer only; `Runtime.RenderExtraction` integration, `MeshGeometry*` diagnostics counters, and dirty-domain reupload are owned by `RUNTIME-085` Slices B and C. Also exports `BuildSurfaceTriangleFaceMap(ConstSourceView, std::vector<std::uint32_t>&)` (`RUNTIME-093`), the inverse of the surface picking payload: it records the owning face row for every surface triangle in the exact order `PackMesh` emits them (the two share one internal `ProduceFaceRing` walk so they cannot drift), so runtime selection refinement can translate a `gl_PrimitiveID`-based `Face` selection-id payload back to a face row even when n-gon faces fan-triangulate to multiple GPU triangles. |
| `Extrinsic.Runtime.MeshPrimitiveViewPacker` | Runtime-authored derivation of optional mesh *edge* (line-list) and *vertex* (point) render views from the same authoritative mesh `GeometrySources` (`RUNTIME-088` Slice A). Exports `MeshPrimitiveViewSettings` (the runtime/editor control surface: `EnableEdgeView` / `EnableVertexView` + `AnyEnabled()`), `MeshPrimitiveVertex` (20-byte `{pos.xyz, uv}` layout matching `MeshVertex`/`GraphVertex`/`PointCloudVertex`), `MeshPrimitiveViewBuffer` (caller-owned `VertexBytes`+`LineIndices` scratch reused across ticks), `MeshPrimitiveViewStatus` (`Success`, `WrongDomain`, `MissingPositions`, `EmptyMesh`, `MissingEdgeTopology`, `InvalidEdge`, `NonFinitePosition`), `MeshPrimitiveViewResult`, `DebugNameForMeshPrimitiveViewStatus()`, and the two entry points `PackMeshEdgeView(ConstSourceView, MeshPrimitiveViewBuffer&)` / `PackMeshVertexView(ConstSourceView, MeshPrimitiveViewBuffer&)`. Both require `ActiveDomain == Domain::Mesh` and read mesh vertex positions (`v:position` on `Vertices`) into one shared vertex buffer in input order; the edge view additionally emits a validated `(e:v0, e:v1)` line-list from `Edges` (a mesh with zero edges is valid and yields no line indices), while the vertex view emits no index buffer (the retained point pipeline draws the vertex buffer directly). Deleted vertex/edge rows are packed in place (no compaction) in this slice, mirroring the graph/point packers. `LocalBounds.LocalSphere` is filled from the vertex AABB midpoint and half-diagonal; `WorldSphere`/`WorldAabb*` stay zero (runtime extraction overwrites them via `ExtractBounds`). Slice A delivered the standalone packers + control vocabulary; `RUNTIME-088` Slice B wired the `RenderExtractionCache` residency: the cache-owned `MeshPrimitiveViewSettings` control surface, per-view `GpuWorld` instance + `GpuGeometryHandle` sidecars over the parent mesh, dirty-domain reupload on the shared mesh dirty signal, enable/disable + eligibility-flip + destruction + shutdown release, the `TickMeshPrimitiveViewGeometry` deferred-retire window, and the `Mesh{Edge,Vertex}View*` + `MeshPrimitiveViewFreeRetires` diagnostics counters — see the residency prose below. UI-001 Slice C exposes `Engine::{Set,Get,Clear}MeshPrimitiveViewSettings` wrappers so editor commands route through runtime-owned extraction state rather than ECS components or graphics handles. |
| `Extrinsic.Runtime.GraphGeometryPacker` | Runtime-authored graph `GeometrySources` → `GpuWorld::GeometryUploadDesc` packer (`RUNTIME-086` Slice A). Exports `GraphVertex` (20-byte `{pos.xyz, uv}` layout matching `MeshVertex`), `GraphPackBuffer` (caller-owned scratch reused across ticks), `GraphPackStatus` (`Success`, `WrongDomain`, `NoRenderLane`, `MissingNodes`, `EmptyGraph`, `MissingEdgeTopology`, `InvalidEdge`, `NonFinitePosition`), `GraphPackResult`, `PackGraph(ConstSourceView, wantLines, wantPoints, GraphPackBuffer&)`, and `DebugNameForGraphPackStatus()`. `PackGraph` reads node positions (`v:position` on `Nodes`) into one shared vertex buffer in input order so edge endpoints index directly into it; the point lane (`wantPoints`) draws the vertex buffer directly, the line lane (`wantLines`) emits a line-list `LineIndices` of validated `(e:v0, e:v1)` endpoint pairs (a graph with zero edges is valid and yields no line indices). Deleted node/edge rows are packed in place (no compaction) in this slice. `LocalBounds.LocalSphere` is filled from the node AABB midpoint and half-diagonal; `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites them via `ExtractBounds`. One `GpuGeometryHandle` per graph entity carries both lanes, matching the canonical single-renderable-instance contract. `Runtime.RenderExtraction` integration, `GraphGeometry*` diagnostics counters, eligibility-flip releases, dirty-domain reupload, and the `TickGraphGeometry` deferred-retire window landed in `RUNTIME-086` Slices B + C — see the residency prose below. |
| `Extrinsic.Runtime.PointCloudGeometryPacker` | Runtime-authored point-cloud `GeometrySources` → `GpuWorld::GeometryUploadDesc` packer (`RUNTIME-087`). Exports `PointCloudVertex` (20-byte `{pos.xyz, uv}` layout matching `MeshVertex`/`GraphVertex`), `PointCloudPackBuffer` (caller-owned scratch reused across ticks), `PointCloudPackStatus` (`Success`, `WrongDomain`, `MissingPositions`, `EmptyCloud`, `NonFinitePosition`), `PointCloudPackResult`, `PackCloud(ConstSourceView, PointCloudPackBuffer&)`, and `DebugNameForPointCloudPackStatus()`. `PackCloud` reads point positions (`v:position` on `Vertices`) into one vertex buffer in input order; the retained point pipeline draws the vertex buffer directly so no index buffer is emitted (`SurfaceIndices`/`LineIndices` stay empty). Deleted vertex rows are packed in place (no compaction) in this slice. `LocalBounds.LocalSphere` is filled from the point AABB midpoint and half-diagonal; `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites them via `ExtractBounds`. One `GpuGeometryHandle` per cloud entity, matching the canonical single-renderable-instance contract. `Runtime.RenderExtraction` integration, `PointCloudGeometry*` diagnostics counters, eligibility-flip releases, dirty-domain reupload, and the `TickPointCloudGeometry` deferred-retire window also land in `RUNTIME-087` — see the residency prose below. |
| `Extrinsic.Runtime.PrimitiveSelectionRefinement` | Runtime-owned CPU refinement that converts a graphics `EncodedSelectionId` primitive hint into an authoritative mesh face/edge/vertex, graph edge/node, or point-cloud point selection against the entity's promoted `GeometrySources` view (`RUNTIME-093` Slice A). Exports `RefinedPrimitiveKind` (`None`/`Entity`/`Face`/`Edge`/`Vertex`/`Point`; a graph node is reported as `Vertex`), the fail-closed `PrimitiveRefineStatus` taxonomy (`Success`, `CpuFallbackResolved`, `UnsupportedDomain`, `StaleEntity`, `MissingGeometrySource`, `InvalidPrimitivePayload`, `CpuFallbackMiss`) with `DebugNameForPrimitiveRefineStatus()` and the `IsResolved()` predicate, `kInvalidPrimitiveIndex`, the `PrimitiveRefineRequest` input (echoed `EntityId`/`StableId`, the GPU `Hint`, an `EntityIsLive` liveness flag, an optional entity-local `LocalHit` anchor, the optional entity-local pick-ray fallback inputs (`HasPickRay`/`RayOrigin`/`RayDirection`/`FallbackRadius`), and the `LocalToWorld` transform), the `PrimitiveSelectionResult` (status + domain + kind + `FaceId`/`EdgeId`/`VertexId`/`PointId` (`kInvalidPrimitiveIndex` when N/A) + consistent `LocalHit`/`WorldHit` when `HasHitPosition`), and the pure, stateless `RefinePrimitiveSelection(ConstSourceView, PrimitiveRefineRequest)` entry point. The hint is treated as a hint only: every payload index is validated against authoritative CPU geometry, a `!EntityIsLive` request is rejected as `StaleEntity` before any geometry is touched, and a hint primitive domain that does not apply to the geometry domain (e.g. a `Face` hint on a graph or point cloud) fails closed as `UnsupportedDomain`. Mesh refinement maps a `Face` hint — whose 28-bit payload is the GPU per-draw triangle index (`gl_PrimitiveID`, written by `assets/shaders/selection/face_id.frag`), **not** a face row — through `Extrinsic.Runtime.MeshGeometryPacker::BuildSurfaceTriangleFaceMap` (the shared inverse of `PackMesh`'s n-gon fan-triangulation, so a quad's second triangle resolves to the quad face instead of being rejected) to the owning face row, and then, when a `LocalHit` anchor is present, walks that face's halfedge ring (`f:halfedge`/`h:next`/`h:to_vertex`, step-capped against malformed topology) to report the nearest face vertex and the nearest boundary edge (resolved to an `Edges` row via unordered endpoint match); an `Edge` hint reports the edge plus the nearest endpoint, and a `Point` hint resolves a mesh vertex (RUNTIME-088 vertex point view). Graph refinement resolves `Edge` → edge + nearest endpoint node and `Point` → node; point-cloud refinement resolves `Point` → point. The reported hit position is the request's local anchor when present, otherwise the resolved primitive's representative local position (vertex/point position, edge midpoint, face centroid), transformed by `LocalToWorld` so both local and world hit data are consistent. When the hint carries no usable sub-primitive (`SelectionPrimitiveDomain::None`, e.g. an all-zero "no hit" `EncodedSelectionId`) and the request supplies an entity-local pick ray, refinement runs the optional CPU ray fallback (`RUNTIME-093` Slice B1): it resolves the entity's nearest mesh vertex / graph node (reported as `Vertex`) / point-cloud point whose perpendicular distance to the ray (a half-line clamped at the origin, `t >= 0`) is minimal and within `FallbackRadius`, reporting `CpuFallbackResolved` with that primitive's local position transformed to world, or the deterministic `CpuFallbackMiss` when nothing qualifies, the radius is non-positive with no primitive exactly on the ray, or the ray direction is degenerate. The runtime caller owns the entity transform and transforms a world-space pick ray into local space, so the entry point stays pure and only ever maps local→world for reporting; a valid hint always wins over the ray (the fallback never overrides a resolved `Face`/`Edge`/`Point`/`Entity` hit), and a missing hint with no pick ray stays a fail-closed `UnsupportedDomain`. The module imports the promoted ECS `GeometrySources` view, the ECS `Scene::Registry`/`Transform::WorldMatrix` (for the frame-loop bridge below), and the graphics `EncodedSelectionId`/`SelectionPrimitiveDomain`/`PickReadbackResult` producer types — it never imports live ECS into graphics and mutates no selection state (the caller owns any cache mutation). The module also exports the frame-loop bridge `RefinePickReadbackResult(Scene::Registry&, const Graphics::PickReadbackResult&)` (`RUNTIME-093` Slice B2): it resolves the readback's render id (`StableEntityLookup::ToRenderId`: `entt::entity` handle + 1, `0` = background) to a live entity by decode + a `registry.valid()` version check (a recycled/destroyed slot yields a deterministic `StaleEntity` rather than refining its new occupant), reads the entity's `Transform::WorldMatrix` as `LocalToWorld` (identity when absent), builds the authoritative `ConstSourceView` from the live registry, and delegates to `RefinePrimitiveSelection`; a background (no-hit) readback resolves to `std::nullopt`. It is a pure read — it mutates neither the registry nor any selection state, so the caller owns the editor-facing cache. BUG-026 adds the context-aware overload `RefinePickReadbackResult(scene, readback, const PickReadbackContext*)` plus the exported `UnprojectPickDepth(...)` helper: `Engine` captures a per-`Sequence` `PickReadbackContext` (the issuing frame's inverse view-projection, viewport, world pick ray, and pixel-radius scale) when it drains a pick, and replays it when the matching readback arrives, so the readback's `SceneDepth` sample reconstructs the world-space cursor position against the camera that issued the pick. The reconstructed cursor is reported in both spaces on the result (`CursorFromDepth`, `WorldCursor`, `LocalCursor`, `Depth`), feeds the entity-local `LocalHit` anchor (driving the nearest-vertex/edge resolution on the hinted face/edge), and supplies the entity-local pick ray + pixel-footprint `FallbackRadius` for the missing-hint CPU fallback (perspective cameras scale the pixel radius by the hit distance; orthographic cameras — detected via the exported `IsOrthographicProjection`, e.g. the promoted `TopDownCameraController` — keep the depth-invariant `orthoHeight / viewportHeight` footprint so a top-down pick radius does not grow with camera altitude). Slice A delivers the standalone hint-based refinement core + `contract;runtime` fixture coverage (`Scaffolded`); Slice B1 adds the optional CPU ray fallback for missing hints (`CpuFallback*` statuses) + fallback `contract;runtime` coverage; Slice B2 wires the bridge into `Engine::RunFrame` (the editor-facing `m_LastRefinedPrimitive` cache exposed by `Engine::GetLastRefinedPrimitiveSelection()`, updated from each pick readback as the readback-drain loop consumes it — newest pick wins, a background readback clears it, an empty-drain frame retains the prior value) and closes `CPUContracted`. |
| `Extrinsic.Runtime.ReferenceScene` | Opt-in runtime-owned reference scene seam (GRAPHICS-029A/B, updated by RUNTIME-097). Exports `IReferenceSceneProvider`, `ReferenceSceneRegistry`, `ReferenceSceneEntity`/`ReferenceScenePopulation`, `TriangleProvider`, `MakeDefaultReferenceSceneRegistry()`, `RegisterDefaultReferenceProvidersIfAbsent()`, and `BuildReferenceCameraViewInput()`. `Engine::Initialize()` invokes `RegisterDefaultReferenceProvidersIfAbsent` so any unregistered selector receives its production default (currently `TriangleProvider` for `Triangle`), then resolves `EngineConfig::ReferenceScene::Selector` against `Engine::GetReferenceSceneRegistry()` exactly once after scene-registry construction and before `IApplication::OnInitialize`. The returned `ReferenceScenePopulation` is stored so `Engine::Shutdown()` routes teardown through the same provider before the scene registry is destroyed; the optional `CameraViewInput` seed is captured on `m_ReferenceCamera` and consumed as the initial state for `Extrinsic.Runtime.CameraControllers`. `m_ReferenceSceneInstalled` guards against double-install via `std::terminate`, and `ReferenceSceneRegistry::Resolve()` itself terminates on unregistered selectors (GRAPHICS-029 Decision 7 applied to both register and resolve). `TriangleProvider::Populate` calls `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")`, stamps a durable `StableId`, `Selection::SelectableTag`, `Graphics::Components::RenderSurface{Domain = Vertex}`, and white `Graphics::Components::VisualizationConfig`, populates mesh-domain `GeometrySources` from one finite halfedge triangle, and returns a CameraViewInput seed (position (0,0,3), forward (0,0,-1), up (0,1,0), near 0.1, far 100). |
| `Extrinsic.Runtime.RenderExtraction` | Runtime-owned ECS-to-graphics extraction cache and snapshot handoff |
| `Extrinsic.Runtime.RenderWorldPool` | Runtime-owned multi-buffer slot-lifecycle pool for pipelined frames (`GRAPHICS-036A`, first implementation child of the retired `GRAPHICS-036` planning slice; the planning slice named it `GRAPHICS-036-Impl-A`). Exports `RenderWorldPoolDiagnostics` (the three `GRAPHICS-036` decision-7 counters: `PipelineStallCount`, `ExtractionSkipCount`, `LastConsumedFrameAge`) and the `RenderWorldPool` value type. Implements the producer/consumer slot state machine the planning slice calls "atomic swap primitives + reclamation queue": the producer (extraction) calls `AcquireBack(frameIndex)` for a free slot, writes the snapshot, and `PublishFront(slot)` (release store of a single `std::atomic` front index plus a monotonic publish-sequence bump); the consumer (renderer) calls `AcquireFront(frameIndex)` (acquire load, per-slot atomic refcount increment) and `ReleaseFront(slot)`. Buffer count defaults to 3 (triple-buffer with reclamation, decision 1), clamps to `[1, 4]`, and collapses to in-place synchronous reuse at 1. Reclamation (decision 4) returns a slot to the free list only once its refcount is zero and it is no longer the published front, drained at the start of each `AcquireBack`. Back-pressure (decision 5): producer-faster overwrites the still-unpublished back slot (`ExtractionSkipCount`); consumer-faster reuses the current front when no new publish-sequence is observed (`PipelineStallCount`), so a synchronous pool that re-publishes the same slot index every frame is never mistaken for a stall. When the producer outruns the consumer so far that every slot is a published front still held in flight (no free slot and no unpublished back), `AcquireBack` fails closed — it returns `kInvalidSlot` (still counting `ExtractionSkipCount`) so the extraction is skipped and the previous front stays current, rather than overwrite storage an in-flight frame still references. The module imports nothing from graphics/ECS/platform — it manages only slot indices and atomics, introducing no new dependency edge. `GRAPHICS-036D` extends the CPU contract to the pipelined integration path: the renderer retains per-slot snapshot storage keyed by the pool slot, and `RenderConfig::SynchronousExtraction = false` consumes `AcquirePreviousFront` to prove render-N-1 without stalls/skips while synchronous mode remains the default. `GRAPHICS-036B` surfaces the pool's three counters read-only on `RuntimeRenderExtractionStats` (`RenderWorldPipelineStallCount`, `RenderWorldExtractionSkipCount`, `RenderWorldFrameAgeFrames`) via the pure `MirrorRenderWorldPoolDiagnostics(pool, stats)` free function in `Extrinsic.Runtime.RenderExtraction`. |
| `Extrinsic.Runtime.SelectionController` | Runtime/editor-owned selection authority (`RUNTIME-089` Slice A). Exports `SelectionPickMode` (`Replace`/`Add`/`Toggle`), `SelectionPickKind` (`Hover`/`Click`), `PendingSelectionPick`, `SelectionControllerConfig`, `SelectionControllerDiagnostics`, and the `SelectionController` class. Input ports call `RequestHoverPick`/`RequestClickPick`; the controller coalesces same-frame pointer events into one `PendingSelectionPick` (a click supersedes any pending hover, latest position wins). `ConsumePendingPick` drains the survivor, assigns it a unique monotonic `Sequence`, and tracks it in a bounded FIFO of in-flight picks (multiple can be outstanding because GPU picking runs several frames in flight). `ConsumeHit(registry, stableEntityId, pickSequence)` / `ConsumeNoHit(registry, pickSequence)` resolve the exact request that produced the readback by `Sequence` and replay its kind/mode, so a hover readback only touches `HoveredTag` and a click readback only touches `SelectedTag` even when several picks are in flight or the renderer publishes results out of issue order. The correlation `Sequence` is threaded end-to-end (`PickPixelRequest` → `RenderWorld.PickRequest` → the renderer's picking slot → `PickReadbackResult`), and `SelectionSystem` holds completed readbacks in a **FIFO queue** drained by `PopPickResult` (replacing the prior single last-result holder that dropped all but the newest when `Graphics.Renderer::DrainCompletedPickingSlots` published several slots in one `BeginFrame`). No-sequence convenience overloads consume the oldest in-flight pick for callers with at most one outstanding (or uncorrelated, `Sequence == 0`, readbacks); the in-flight FIFO is bounded by `MaxTrackedInFlightPicks` (oldest evicted, `InFlightPicksDropped` bumped) so a readback that is never published cannot leak, and an unmatched sequence is applied as a configured-mode click (`UntrackedReadbacks`). Hits resolve the runtime stable entity id to a live `entt::entity` through the render-id resolution seam — when `Engine` has attached a `StableEntityLookup` (`SetStableEntityLookup`, `RUNTIME-092` Slice B) the seam routes through the sidecar's `ResolveByRenderId` (decode + live-registry validation, so a recycled/destroyed slot is rejected by the runtime-owned authority); standalone (no attached lookup) it falls back to the `ToEntityHandle` decode plus the controller's validity check. Hits reject stale (destroyed) and non-`SelectableTag` entities, and apply the documented Replace/Add/Toggle policy to the insertion-ordered selection set. The controller mirrors that set onto ECS `Selection::SelectedTag`/`HoveredTag` and maintains the `SelectedStableIds()`/`HasHovered()`/`HoveredStableId()` snapshot buffers that Slice B copies into `RenderWorld.Selection`. Default sandbox policy: single-select click, hover outline, additive/toggle via the click-mode argument, clear-on-background click (Replace mode) and clear-on-background hover. The controller module itself imports only the promoted ECS registry/handle and selection components — never graphics, platform input, or the renderer. Slice B (RUNTIME-089) wires it into the frame loop from `Engine::RunFrame`, which owns the graphics handoff: input ports/editor tools submit picks onto `Engine::GetSelectionController()`, the coalesced pick is drained (carrying its correlation `Sequence`) into `RenderFrameInput::Pick`/`HasPendingPick` and `SelectionSystem::RequestPick` before `IRenderer::ExtractRenderWorld()`, **all** completed readbacks are drained FIFO via `SelectionSystem::PopPickResult()` in the maintenance phase after present and each is resolved by its `Sequence` through the sequence-correlated `ConsumeHit`/`ConsumeNoHit` overloads (so out-of-order / multiple-in-flight readbacks each apply to the correct request), and the controller snapshot (`SelectedStableIds()`/`HoveredStableId()`/`HasHovered()`) is mirrored into `RenderWorld.Selection` via `RenderExtractionCache::ExtractAndSubmit(..., const SelectionController* selection)` → `RuntimeRenderSnapshotBatch::Selection*` → `SubmitRuntimeSnapshots` stable storage → `ExtractRenderWorld`, so graphics stays reporting-only and never reads live ECS. The selection snapshot's outline styling on `Graphics::SelectionSnapshot` keeps its recipe defaults; only the identity fields are filled here. The real input→pick binding (which mouse button/modifier maps to click vs hover and Add/Toggle) is owned by a later editor/UI task; Slice B proves the plumbing through `contract;runtime` coverage. |
| `Extrinsic.Runtime.StableEntityLookup` | Runtime-owned scene-local lookup sidecar (`RUNTIME-092`, Slice A). Exports `StableEntityLookupDiagnostics` and the `StableEntityLookup` class. Maps the optional, durable `ECS::Components::StableId` of an entity to its current live `entt::entity` (`HARDEN-068` Decision 3 left this `StableId -> entity` lookup to a runtime consumer; this is that consumer). Two resolution paths: **by `StableId`** uses a stored, maintained winner-map because `StableId` is independent of the `entt::entity` bit pattern (it survives recycling / save-load); **by render/extraction stable id** (`std::uint32_t`) decodes the id — which is `static_cast<std::uint32_t>(entt::entity) + 1`, the shifted reversible index+version encoding emitted by `RenderExtractionCache::StableEntityId` / `SelectionController::ToStableEntityId` (render id `0` is reserved for the GPU picking background sentinel, BUG-026) — and validates the handle against the registry (a recycled slot carries a bumped version, so a stale render id fails `IsValid`). `Rebuild(registry)` re-derives the winner-map from every live entity with a valid `StableId`; `Track`/`Forget` maintain it incrementally; `ResolveByStableId`/`ResolveByRenderId`/`ResolveSelected` resolve to live entities; `PruneStale` drops destroyed winners in bulk. Imports only the promoted ECS registry / handle and the `StableId` value type — never graphics or platform — and adds no lookup state to ECS. Slice A delivered the standalone sidecar (`Scaffolded`); Slice B (`RUNTIME-092`, closes `CPUContracted`) wired the rebuild into the runtime frame lifecycle (`Engine` owns the lookup, `Rebuild`s it each frame before the pick-readback drain) and routed `SelectionController`'s render-id resolution seam onto the sidecar's `ResolveByRenderId`. See the duplicate/stale policy and frame-wiring notes below. |
| `Extrinsic.Runtime.SpatialDebugAdapters` | Runtime-only translation seam from geometry-tree implementations (`Geometry.BVH`, `Geometry.KDTree`, `Geometry.Octree`, `Geometry.ConvexHull`) into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers` packet types (clarified by `GRAPHICS-011Q`; tracked by `RUNTIME-082`). Exports `SpatialDebugSnapshotBatch` (mutable per-frame output container with `Bounds`, `HierarchyNodes`, `SplitPlanes`, `ConvexHullVertices`, `ConvexHullEdges`, `PointMarkers` spans plus a `Clear()` helper), `SpatialDebugAdapterOptions` (`LeafOnly`, `OccupancyOnly`, `MaxDepth`), `SpatialDebugAdapterStats` (per-`Append` accumulator with `LeafNodeCount`, `InnerNodeCount`, `SplitPlaneCount`, `EmptyNodeSkippedCount`, `DepthCapTruncationCount`), the pure-virtual `ISpatialDebugAdapter` interface, the `BvhAdapter` (Slice A), `KdTreeAdapter`, `OctreeAdapter` (Slice B), `ConvexHullAdapter` (Slice C) concretes, and the `SpatialDebugAdapterRegistry` (Slice C) key→adapter table. Runtime is the only layer permitted to import both geometry tree implementations and the graphics packet types per `AGENTS.md` §2. `KdTreeAdapter` mirrors the BVH pattern (one `SpatialDebugSplitPlane` per inner node carrying `node.SplitAxis`/`node.SplitValue`); `OctreeAdapter` emits three perpendicular `SpatialDebugSplitPlane`s per inner node — one per axis at the parent AABB center — because `Geometry::Octree::Node` does not record the chosen split point. The center-based visualization is exact for `SplitPoint::Center` and an explicit approximation for `Mean`/`Median`, which the adapter does not attempt to reconstruct. `ConvexHullAdapter` copies the hull's V-Rep into `ConvexHullVertices` and derives `ConvexHullEdges` by plane-incidence (two vertices form a `SpatialDebugWireEdge` when they share ≥2 face planes within `IncidenceEpsilon`); it ignores `LeafOnly`/`OccupancyOnly`/`MaxDepth` and leaves `SpatialDebugAdapterStats` untouched because none of the tree-shaped concepts apply to a flat hull. `SpatialDebugAdapterRegistry` maps an opaque `std::uint64_t` renderable key onto a non-owning `const ISpatialDebugAdapter*`; callers own adapter lifetime and must `Unregister` before the adapter or its source geometry tree is destroyed. `RUNTIME-082` Slice A scaffolded the umbrella + BvhAdapter + value types; Slice B added the KdTree/Octree adapters; Slice C landed the ConvexHull adapter + registry; Slice D wires the pump through `RenderExtractionCache::ExtractAndSubmit` (`RegisterSpatialDebugAdapter` / `UnregisterSpatialDebugAdapter` transfer `std::unique_ptr<ISpatialDebugAdapter>` ownership into the cache and mirror into an embedded `SpatialDebugAdapterRegistry`; the extraction loop walks the `ECS::Components::SpatialDebugBinding` view, accumulates a shared `SpatialDebugSnapshotBatch`, attaches its spans to `RuntimeRenderSnapshotBatch::SpatialDebug{Bounds,HierarchyNodes,SplitPlanes,ConvexHullVertices,ConvexHullEdges,PointMarkers}`, and folds per-frame counters onto `RuntimeRenderExtractionStats`'s `SpatialDebug{BindingsObserved,AdaptersInvoked,MissingAdapterCount,BoundsCount,HierarchyNodeCount,SplitPlaneCount,ConvexHullVertexCount,ConvexHullEdgeCount,PointMarkerCount,LeafNodeAccumulator,InnerNodeAccumulator,EmptyNodeSkippedAccumulator,DepthCapTruncationAccumulator}` fields). The Slice D follow-up wires `Graphics::Renderer::SubmitRuntimeSnapshots` to consume those spans via the existing `BuildSpatialDebug{Hierarchy,Bounds,SplitPlane,ConvexHull,PointMarkers}Wireframes` helpers (preferring `HierarchyNodes` over `Bounds` when both are populated 1:1, to avoid double-rendering the same node bounds), routes the produced `Debug{Line,Point,Triangle}Packet` records through the same validation+clamp loop the explicit runtime debug spans use, and surfaces them via `RenderWorld::DebugPrimitives` so the canonical debug-primitive pass actually rasterizes the wireframes. |
| `Extrinsic.Runtime.VisualizationAdapters` | Runtime-only producer seam from CPU geometry/property metadata into data-only `Extrinsic.Graphics.VisualizationPackets` records (delivered by retired `RUNTIME-083`). Slice A exports `VisualizationAdapterBatch` (mutable vectors for all visualization packet lanes plus `Clear()` and `AsPacketBatch()` span view), `VisualizationAdapterOptions` (source/output name, expected attribute domain, externally owned buffer device addresses, auto/manual range, colormap, overlay metadata, atlas metadata, and fragment-bake mapping), `VisualizationAdapterStats`, the pure-virtual `IVisualizationAdapter`, `PropertyScalarAdapter`, and `VisualizationAdapterRegistry`. `PropertyScalarAdapter` reads a `Geometry::ConstPropertySet` float or double property, computes a deterministic scalar range from finite CPU values (expanding flat auto-ranges to a valid non-zero interval), and emits a `Graphics::ScalarAttributePacket` using the caller-provided GPU buffer address. Slice B wires scalar-field extraction through `RenderExtractionCache`: the cache owns adapter instances via `RegisterVisualizationAdapter` / `UnregisterVisualizationAdapter`, stores per-renderable `VisualizationAdapterBinding` records keyed by stable entity id, invokes the bound adapter when `Graphics::Components::VisualizationConfig::Source == ScalarField`, attaches the resulting packet spans to `RuntimeRenderSnapshotBatch::Visualization*`, and reports adapter invocation/rejection plus packet-lane counters on `RuntimeRenderExtractionStats`. Runtime owns this translation seam; it does not create GPU buffers from CPU `PropertySet` data and requires bindings to supply the external buffer device address. Slice C.1 adds `KMeansLabelAdapter` and `VectorFieldAdapter`: `KMeansLabelAdapter` maps finite `glm::vec4` color properties such as `v:kmeans_color` into `Graphics::ColorAttributePacket`, and `VectorFieldAdapter` maps finite `glm::vec3` vector properties plus caller-provided position/vector buffer BDAs, scale, color, and depth-test policy into `Graphics::VectorFieldOverlayPacket`. These C.1 adapters validate source shape and metadata but do not schedule KMeans, allocate vector-field residency, or wire non-scalar extraction selection. Slice C.2 adds `IsolineAdapter`: it reads finite float/double scalar properties, computes or validates the scalar range, validates isoline count, line width, color, and depth-test metadata, and emits `Graphics::IsolineOverlayPacket` records. The C.2 adapter does not import legacy graphics isoline extraction, allocate overlay residency, or wire non-scalar extraction selection. Slice D adds `HtexMetadataAdapter`: it emits `Graphics::HtexPatchPreviewAtlasPacket` and `Graphics::FragmentBakeAtlasPacket` records from caller-authored atlas metadata, validates texcoord requirements for UV bakes, and schedules a deterministic `StreamingExecutor` task for valid `RecreateHtex` requests. Slice E extends `RenderExtractionCache::VisualizationAdapterBinding` with packet-kind/option metadata, selects scalar/color/isoline metadata from `VisualizationConfig` where applicable, accepts vector/Htex metadata from runtime-owned binding options, attaches non-scalar spans to `RuntimeRenderSnapshotBatch::Visualization*`, and folds non-scalar packet/error counters onto `RuntimeRenderExtractionStats`. The umbrella remains CPU-contracted: it does not allocate atlas residency, run an Htex regeneration algorithm inline, or claim backend visual proof. |
| `Extrinsic.Runtime.ImGuiAdapter` | Runtime-side Dear ImGui platform/renderer adapter (`RUNTIME-090` Slice A, the producer half declared by `GRAPHICS-013CQ`). Exports `ImGuiAdapterDiagnostics` (testable observables: `Initialized`, `FramesProduced`, `LastDrawListCount`/`LastVertexCount`/`LastIndexCount`/`LastCommandCount`, `LastFrameUsedUserTexture`, `PumpedEventCount`, `ContextRebuilds`, `EditorCallbackInvocations`, `DisplayWidth`/`DisplayHeight`) and the `ImGuiAdapter` class, constructed with `(Platform::IWindow&, Graphics::ImGuiOverlaySystem&)`. `Initialize()` creates the ImGui context, calls `ImGuiOverlaySystem::Initialize()`, and configures ImGui IO; `BeginFrame(deltaSeconds)` refreshes display metrics from the window, pumps the window's drained `Platform::Event`s into ImGui IO, and calls `ImGui::NewFrame()`; `EndFrame()` invokes the editor hook, calls `ImGui::Render()`, walks `ImDrawData` into one `ImGuiOverlayFrame` (font-atlas bytes, per-list command/vertex/index counts, copied POD vertex/index payloads, and `UsesUserTexture` derived by comparing each draw command's texture id against the font atlas), and submits it via `ImGuiOverlaySystem::SubmitFrame`; `Shutdown()` tears the overlay system + context down; `RebuildForDisplayChange()` runs exactly one `Shutdown()`+`Initialize()` cycle (DPI/font rebuild) counted in `ContextRebuilds`; `SetEditorCallback(std::function<void()>)` registers the editor hook called once per frame between begin and end. The adapter is backend-agnostic: it translates `Platform::Event` variants (cursor/button/scroll/char/resize) into ImGui IO directly rather than linking `imgui_impl_glfw`, so it drives the `Null` window as well as the GLFW backend. GLFW key-code→`ImGuiKey` translation is deferred to the editor input-binding slice (counted as pumped but not translated). ImGui is version `1.92.5`; the adapter intentionally keeps `ImGuiBackendFlags_RendererHasTextures` disabled for now because the promoted renderer consumes a copied legacy CPU font atlas through `ImGuiOverlaySystem` rather than processing `ImDrawData::Textures[]` dynamic texture requests. `imgui.h` stays out of the `.cppm` interface (only an opaque `ImGuiContext*` forward declaration is used) and `imgui_core_lib` (backend-agnostic, no glfw/volk) is linked **PRIVATE** to `ExtrinsicRuntime`, so graphics never sees ImGui types and the runtime's public surface gains no windowing/Vulkan deps. Slice A delivers the standalone adapter module + `FakeWindow`-driven `contract;runtime` coverage (`Scaffolded→CPUContracted`). Slice B wires it into `Engine` and closes `CPUContracted`: `Engine` owns the `Graphics::ImGuiOverlaySystem` instance (runtime owns composition; the allowed `runtime -> graphics` edge) and constructs the adapter in `Initialize()` after the `Window` and `Renderer` exist. `Engine::RunFrame` calls `BeginFrame(frameDt)` after `Window::PollEvents` and the minimize/resize early returns — immediately before `IApplication::OnVariableTick` — and `EndFrame()` immediately after the variable tick and before the render contract's `IRenderer::PrepareFrame()`, so exactly one `ImGuiOverlayFrame` is produced per engine frame and a minimized frame never leaves a `NewFrame()` without a matching `Render()`. The editor hook is exposed from `Engine::SetImGuiEditorCallback(std::function<void()>)` (applied to the adapter at construction so it may be registered before or after `Initialize()`), with a read-only `Engine::GetImGuiAdapter()` observer for the produce-path diagnostics; `Engine::Shutdown()` tears the adapter down first while the `Window` and overlay system it borrows are still alive. GRAPHICS-079 Slice B hands the same engine-owned overlay instance to the renderer via `IRenderer::SetImGuiOverlaySystem`, so producer and consumer share one instance. GRAPHICS-079 Slice C consumes the adapter's copied font-atlas/vertex/index payloads through the renderer-owned retained atlas and transient upload helper; GRAPHICS-079 Slice D promotes the pass to `FrameRecipe.PresentSource` and carries per-command user-texture bindless indices through the renderer pass and shader sampling. |
| `Extrinsic.Runtime.SandboxEditorUi` | Runtime-owned sandbox editor shell (`UI-001` Slices A-D, extended by `UI-002`, `UI-003`, `UI-004`, `UI-005`, `UI-006`, and `RUNTIME-098`). Exports data-only panel/domain-window models, geometry-processing capability records, deterministic missing-dependency diagnostics, primitive-detail presentation helpers, file/import and scene-file command/result DTOs, transform-edit/camera-controller/primitive-view/spatial-debug/visualization-config/visualization-property/visualization-adapter-binding command DTO/status types, visualization-property metadata DTOs, CPU K-Means command/result DTOs, `SandboxEditorDomainWindowKind`, `SandboxEditorDomainWindowModel`, `SandboxEditorRenderGraphModel`, `BuildSandboxEditorPanelFrame(...)`, `BuildSandboxEditorDomainWindowModel(...)`, `SelectSandboxEditorEntity(...)`, `ApplySandboxEditorFileImportCommand(...)`, `ApplySandboxEditorSceneSaveCommand(...)`, `ApplySandboxEditorSceneLoadCommand(...)`, `ApplySandboxEditorTransformEdit(...)`, `ApplySandboxEditorCameraControllerCommand(...)`, `ApplySandboxEditorPrimitiveViewCommand(...)`, `ApplySandboxEditorSpatialDebugBindingCommand(...)`, `ApplySandboxEditorVisualizationConfigCommand(...)`, `ApplySandboxEditorVisualizationPropertyCommand(...)`, `ApplySandboxEditorVisualizationAdapterBindingCommand(...)`, `ApplySandboxEditorKMeansCommand(...)`, `DrawSandboxEditorPanelFrame(...)`, and `SandboxEditorUi::Attach(Engine&)`/`Detach()`. The shell attaches through `Engine::SetImGuiEditorCallback`, reads scene/selection/refined-primitive state through runtime-owned APIs, emits selection changes through `SelectionController`, applies transform edits by mutating the selected entity's local `Transform::Component` and stamping `Transform::IsDirtyTag`, submits file/import path commands through `Engine::ImportAssetFromPath(...)` with explicit payload hints for mesh, point-cloud, graph, model-scene, and texture routes, observes `Engine::GetLastAssetImportEvent()` so runtime-handled dropped files update the `File / Import` panel, submits scene save/load path commands through `Engine::SaveSceneToPath(...)` / `Engine::LoadSceneFromPath(...)`, replaces camera-controller slots through the engine-owned `CameraControllerRegistry`, toggles mesh edge/vertex primitive views through runtime extraction-cache settings, routes spatial-debug display options through the selected entity's `ECS::Components::SpatialDebugBinding`, routes material/scalar/color visualization choices through the selected entity's `Graphics::Components::VisualizationConfig`, and routes visualization adapter bindings through `Engine::{Set,Get,Clear}VisualizationAdapterBinding(...)` onto the engine-owned `RenderExtractionCache`. `UI-002` adds promoted ImGui main-menu slots ordered `PointCloud`, `Graph`, and `Mesh`; each menu opens render-hint, visualization, selection-detail, and processing-discovery windows for the selected entity's current `GeometrySources` domain. `UI-003` adds the processing-discovery model: supported mesh, graph, and point-cloud source domains, stable algorithm-entry ordering, K-Means source-domain enumeration, and labels are computed from promoted `GeometrySources`. `UI-004` adds the first execution seam: CPU K-Means can run synchronously for selected mesh vertices, graph nodes, and point-cloud points, publishes `v:kmeans_label`, `v:kmeans_label_f`, `v:kmeans_color`, `p:kmeans_label`, and `p:kmeans_color` properties, stamps `DirtyVertexAttributes`, and reports fail-closed command statuses for invalid targets/inputs. `UI-005` adds promoted visualization-property enumeration and preset routing: selected mesh, graph, and point-cloud visualization windows list scalar, isoline, color-buffer, and vector-candidate properties from current `GeometrySources`, filter promoted internal/connectivity properties for actionable presets, and apply scalar, isoline, and `glm::vec4` color-buffer presets through `VisualizationConfig`. `UI-006` adds the promoted `Frame Graph` diagnostics panel: `BuildSandboxEditorPanelFrame(...)` copies renderer-owned `RenderGraphFrameStats` into a data-only model, and the attached ImGui window displays compile/execute counters, queue/timeline stats, command pass statuses, diagnostics, and the compiler debug dump without UI owning renderer state. Broader algorithm execution, CUDA/asynchronous scheduling, centroid entities, topology mutation, persistent adapter registration from borrowed property views, and generic GPU residency/upload for arbitrary property arrays remain future runtime/editor/renderer work. `RUNTIME-098` adds the ImGui `File / Scene` window with path entry plus save/load buttons backed by the runtime-owned scene command surface. `RUNTIME-102` adds the document model in that window: it reads `EditorCommandHistory::Snapshot()` for dirty state, active path, revision counters, and undo/redo labels, and the buttons call the runtime history service when available. `UI-008` extends `File / Scene` with New, Close, Save / Save As, and Open Path controls, data-only path-entry/native-dialog boundary fields, and an app import-boundary contract that keeps `ExtrinsicSandbox` linked only to runtime. `UI-007` adds the ImGui `File / Import` payload-hint combo and runtime-observed import status for dropped files; UI does not own platform events, file decoding, `AssetService`, or ECS materialization. Mesh render windows can toggle existing runtime-owned edge/vertex primitive-view settings, while graph and point-cloud render windows report the current line/point render lanes; visualization windows reuse the selected-entity spatial-debug, visualization-config, and visualization-property preset command surfaces. Domain-window models report deterministic no-selection, stale-selection, missing-scene/controller, unavailable scene-file commands, command-history, and wrong-domain diagnostics instead of caching authoritative state in UI. Adapter keys, GPU buffer addresses, `VisualizationAdapterOptions`, asset decoding, `AssetService` mutation, model-scene materialization, texture-upload requests, renderer cache state, and scene-document byte transport remain runtime/extraction/asset/core-owned inputs; UI exposes command/model data and deterministic diagnostics without owning simulation, rendering, asset state, or file IO. `imgui.h` stays confined to the `.cpp` implementation unit. Final broad file-backed sandbox visual proof remains downstream of scoped RUNTIME-095 coverage. |
| `Extrinsic.Runtime.GizmoInteraction` | Runtime/editor-owned transform-gizmo interaction (`RUNTIME-084`). Exports `GizmoMode` (`Translate`/`Rotate`/`Scale`), `GizmoAxis`, `GizmoOrientation` (`Global`/`Local`), `GizmoModifier` flags (`Snap`/`Clone`), `PickRay`, `GizmoConfig` (pick radius, axis length, translate/rotate/scale snap and scale clamps), `GizmoHitResult`, full-transform `GizmoTransformEdit` records, `GizmoUndoStack`, `GizmoInteractionDiagnostics`, `GizmoInteraction`, and `TransformGizmoRenderPacketBuilder`. `HitTest(registry, CameraViewSnapshot, cursorPixel, viewport, selected)` projects each axis handle line to screen space and resolves the nearest handle within the pixel pick radius; empty/off-handle picks are a background no-hit. `BeginDrag`/`DragTick`/`DragCommit`/`DragCancel` apply axis-constrained translate/rotate/scale edits by projecting the world `PickRay` onto the locked axis line, mutate ECS authoring transforms in runtime only, stamp `Transform::IsDirtyTag`, latch mode at drag start, honor `Snap`, and emit one before/after position-rotation-scale edit per changed entity on commit. `Engine` owns the interaction, undo stack, packet builder, selected-entity scratch, and default input binding (left mouse drag, left shift snap); extraction forwards the builder's copied `TransformGizmoRenderPacket` span through `RuntimeRenderSnapshotBatch::TransformGizmos`. Graphics sees only the frozen render packet field set and never receives drag state, pointer pixels, modifier keys, undo data, or ECS handles. |
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

`Engine::RunFrame()` keeps per-frame lifecycle state in an internal
`RuntimeFrameContext` record: clamped frame delta, fixed-step interpolation
alpha, monotonic render frame index, `RenderFrameInput`, extraction stats, and
the acquired `RenderWorldPool` front slot. This keeps the stage data explicit
without exporting a runtime API or reviving legacy `Runtime.FrameLoop`,
`Runtime.RenderOrchestrator`, or `Runtime.ResourceMaintenance` modules.

## Derived overlay producer decision

`RUNTIME-104` classifies legacy `Graphics.OverlayEntityFactory` behavior for
current promoted workflows and deliberately adds no `DerivedOverlayProducer`
module. Cross-domain mesh, graph, and point-cloud child overlays are represented
by ordinary promoted `GeometrySources` entities when a runtime/editor command
imports or authors data; mesh edge/vertex overlays use the existing
runtime-owned `MeshPrimitiveViewSettings` sidecar over the parent mesh. Current
vector-field and isoline workflows use `Extrinsic.Runtime.VisualizationAdapters`
to emit data-only visualization packets (`VectorFieldOverlayPacket` and
`IsolineOverlayPacket`) into `RuntimeRenderSnapshotBatch`; that path creates no
child ECS entity, stores no graphics/RHI handle in ECS, and leaves backend
command-shape proof to `GRAPHICS-085`.

The retired overlay snapshot sketches in
`docs/migration/nonlegacy-parity-matrix.md` are historical planning notes, not
current API debt. Reopen a value-gated runtime/editor task only if a concrete
current workflow cannot be represented by ordinary geometry entities,
primitive-view sidecars, transient debug packets, or existing visualization
packet spans.

## Scene lifecycle and persistence boundary

`RUNTIME-100` defines the promoted scene-manager replacement as a runtime-owned
scene replacement boundary, not a broad legacy serializer clone. `Engine` loads a
scene into a temporary registry first; only after successful deserialization does
it drain the old scene's runtime sidecars, replace the registry contents, rebuild
the stable-entity lookup, and reset document history. New/close scene operations
use the same boundary and clear the lookup instead of rebuilding it.

The boundary clears scene-local runtime state:

- `SelectionController::ClearSceneState(...)` drops selected/hovered ECS tags,
  selected-id snapshots, pending picks, and in-flight pick correlation.
- `RenderExtractionCache::ClearSceneState(...)` frees scene-owned renderable
  instances/geometry, collapses deferred retire queues, clears mesh
  edge/vertex-view settings, visualization adapter bindings, transient batches,
  and submits an empty snapshot while preserving registered adapters.
- `PhysicsBridge::Clear()` is the physics-side reset contract: it clears the
  physics world, `StableId -> BodyHandle` sidecar, and fixed-step accumulator.
  `Engine` does not yet own a bridge instance, so engine-integrated physics reset
  remains a wiring decision for the task that installs physics into the frame
  loop.
- `m_LastRefinedPrimitive` and `StableEntityLookup` are reset at the scene
  boundary; load rebuilds the lookup from the loaded scene, new/close clear it.

Persistence support is intentionally narrow and explicit:

| Family | Status | Reason |
|---|---|---|
| Metadata names, stable ids, local transforms, hierarchy links, selectable tags | Supported | Current sandbox/editor scene identity and hierarchy need round-trip coverage. |
| Render surface/line/point hints and `VisualizationConfig` | Supported | These are CPU descriptors consumed by runtime extraction; no live GPU handles are serialized. |
| Mesh/graph/point-cloud `GeometrySources` property data | Supported | These are the current sandbox-authored geometry authorities. |
| Lights and shadow-caster tags | Deferred | Runtime extraction consumes light descriptors, but the current scene file scope does not yet define light/shadow authoring UX or compatibility policy. |
| Collider and rigid-body descriptors | Deferred | ECS authoring descriptors exist and `PhysicsBridge::Clear()` resets live sidecars, but engine-owned physics lifecycle wiring is not installed. |
| `AssetInstance::Source` and arbitrary legacy asset-source reimport | Deferred | Current promoted imports materialize `GeometrySources` or model-scene child assets; arbitrary path reimport needs a value-gated asset ingest state machine. |
| `SpatialDebugBinding` and runtime visualization adapter bindings | Deferred | Bindings point at runtime-owned adapter registries and external buffers, not portable scene data. |
| Camera/editor document state | Retired from scene JSON | Camera controllers and `EditorCommandHistory` are runtime/editor state, not ECS scene contents. |
| Renderer/RHI caches, GPU handles, material bindless slots | Retired from scene JSON | Graphics resources are rebuilt from runtime snapshots and asset ids; scene files never store live backend state. |

Unsupported ECS families encountered during save are counted in
`SceneSerializationStats` and emitted in the JSON `stats` object. They are not
materialized on load, which keeps unsupported persistence fail-closed and
diagnosable rather than silently pretending parity with legacy component dumps.

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
   subscription. Type-specific asset handoffs are constructed after the ECS
   scene exists so model-scene materialization can borrow the scene.
6. ECS `Scene::Registry`, then the runtime constructs
   `AssetModelTextureHandoff` and `AssetModelSceneHandoff`. The texture handoff
   subscribes to texture `Ready` events and requests `GpuAssetCache` texture
   uploads from promoted CPU payloads. The model-scene handoff subscribes to
   model-scene `Ready` events, materializes generated `GeometrySources`
   entities, mints embedded texture child assets, and records material
   `AssetId` bindings. Shutdown resets the model-scene handoff before
   reference-scene teardown or scene reset because it borrows the scene and
   renderer; the texture handoff resets before `AssetService` or
   `GpuAssetCache` destruction.
7. Opt-in reference-scene bootstrap (GRAPHICS-029A/B). The optional camera seed
   is retained for the runtime camera controller created lazily on the first
   render-input snapshot.
8. `IApplication::OnInitialize`.

## Canonical frame loop phases (`Engine::RunFrame`)

1. Platform events / resize handling. Minimized windows wait on platform events,
   resample the frame clock, and return before ImGui or render-frame work begins;
   resize requests idle the device, resize device/renderer resources, acknowledge
   the request, and continue through the normal frame.
2. Fixed-step simulation. Each substep calls `IApplication::OnSimTick`, then
   `RegisterPromotedEcsSystemBundle` to append `TransformHierarchy` /
   `BoundsPropagation` to the CPU `FrameGraph`, and finally compiles and
   executes the graph so dirty world matrices and bounds are recomputed before
   the next substep or render extraction (`RUNTIME-091`).
3. Variable tick.
4. Render input snapshot. After camera resolution and the gizmo-interaction
   drive, the runtime runs the pre-render transform flush
   (`FlushPreRenderTransformState`, BUG-024): `TransformHierarchy` →
   `BoundsPropagation` → `RenderSync` execute directly (outside the fixed-step
   FrameGraph) so post-fixed-step local-transform mutations — Sandbox Editor
   inspector edits applied in the ImGui editor hook, `OnVariableTick()` app
   mutations, and `GizmoInteraction` drags — refresh `Transform::WorldMatrix`,
   world bounds, and `DirtyTags::DirtyTransform` before transform-gizmo packets
   are built and before render extraction observes the scene. The runtime then
   drains the
   coalesced `SelectionController` pick (RUNTIME-089 Slice B): the survivor —
   carrying its correlation `Sequence` — is written into
   `RenderFrameInput::Pick`/`HasPendingPick` (the `Sequence` is threaded on to
   `RenderWorld.PickRequest` and the GPU picking slot) so graphics issues the
   pick this frame.
5. Renderer begin frame. Runtime acquires the render frame through
   `IRenderer::BeginFrame()`. GRAPHICS-040A keeps camera extraction unchanged;
   temporal jitter remains graphics-side, and GRAPHICS-040C maps the renderer's
   AA selector to TAA/external reconstruction, motion-vector recipe activation,
   and retained reconstruction history without adding runtime ownership.
6. Runtime render extraction: the `RenderWorldPool` producer acquires a back
   slot (`AcquireBack(frameIndex)`), then ECS queries, runtime sidecars,
   dirty-domain interpretation, deletion cleanup, the runtime selection
   snapshot mirror into `RenderWorld.Selection` (RUNTIME-089 Slice B, via the
   `SelectionController*` argument to `RenderExtractionCache::ExtractAndSubmit`),
   and `IRenderer::SubmitRuntimeSnapshots(..., storageSlot)` handoff into the
   renderer's matching retained snapshot slot. The producer then publishes the
   slot (`PublishFront`). In synchronous mode the consumer acquires the current
   front (`AcquireFront(frameIndex)`, frame age 0); when
   `RenderConfig::SynchronousExtraction = false`, the consumer acquires the
   previous front (`AcquirePreviousFront(frameIndex)`) so render-N consumes the
   distinct renderer-owned snapshot storage written by extraction N-1.
   `MirrorRenderWorldPoolDiagnostics` copies the pool's three counters onto
   `Engine::GetLastRenderExtractionStats()`.
7. Renderer render-world extraction.
8. Render prepare.
9. Render execute.
10. End frame + present.
11. Maintenance: transfer retirement, streaming drain/apply/pump, asset service tick, `GpuAssetCache::Tick`, `RenderExtractionCache::TickProceduralGeometry` (procedural geometry deferred-retire window), `RenderExtractionCache::TickMeshGeometry` (runtime-owned mesh-residency deferred-retire window), and `RenderExtractionCache::TickGraphGeometry` (runtime-owned graph-residency deferred-retire window), `RenderExtractionCache::TickPointCloudGeometry` (runtime-owned point-cloud-residency deferred-retire window), and `RenderExtractionCache::TickMeshPrimitiveViewGeometry` (runtime-owned mesh edge/vertex primitive-view deferred-retire window). After the maintenance contract the runtime rebuilds the `StableEntityLookup` from the live scene (`RUNTIME-092` Slice B), then drains **all** completed pick readbacks (`SelectionSystem::PopPickResult()` FIFO) into the `SelectionController`, resolving each by its correlation `Sequence` so multiple-in-flight / out-of-order readbacks each apply to the correct request; the controller resolves the render id through the attached lookup (decode + live-registry validation), rejects stale/non-selectable hits, and mutates ECS `Selected`/`Hovered` tags (RUNTIME-089 + RUNTIME-092 Slice B). Each drained readback is also refined into its authoritative sub-primitive via `RefinePickReadbackResult` and cached in the Engine-owned `m_LastRefinedPrimitive` (`Engine::GetLastRefinedPrimitiveSelection()`, `RUNTIME-093` Slice B2): the newest readback wins, a background readback clears it, and an empty-drain frame retains the prior value — graphics never reads this cache (it only produced the hint).
    After the readback drain the `RenderWorldPool` snapshot reference acquired
    in phase 6 is released at frame retire (`ReleaseFront`); this is the current
    front in synchronous mode and the previous front in pipelined mode. In the
    default synchronous mode the single logical slot is reclaimable next frame.
12. Frame clock finalize.

`Engine::RunFrame()` consumes `Extrinsic.Core.FrameClock` for wall-clock frame
delta sampling and post-sleep resampling; runtime owns the phase orchestration,
not the reusable clock value type.

Shutdown is also delegated through `Extrinsic.Core.FrameLoop`: runtime clears
the platform listener, detaches and tears down the ImGui adapter while the window
and overlay system are live, then executes `ExecuteShutdownContract` in this
order: stop running, wait device idle, application shutdown, streaming
shutdown/drain, scene/reference-scene/camera-controller destruction, asset and
GPU-asset handoff destruction, streaming state destruction, frame graph
destruction, render-extraction shutdown plus renderer shutdown, device shutdown,
window destruction, scheduler shutdown, and initialized-state clear.

### Pipelined render-world pool (`GRAPHICS-036C`/`GRAPHICS-036D`)

`Engine` owns a runtime-side `Extrinsic.Runtime.RenderWorldPool` constructed in
`Initialize()` and sized from `Core::Config::RenderConfig::SynchronousExtraction`
(default `true`): one logical buffer in the synchronous baseline, or the
triple-buffered default when pipelined extraction is requested. `RunFrame` drives
the slot lifecycle — `AcquireBack`/`PublishFront` around `ExtractAndSubmit`,
`AcquireFront` for synchronous consume, `AcquirePreviousFront` for the
render-N-1 consume path, and `ReleaseFront` at frame retire — and mirrors the
pool's `PipelineStallCount`/`ExtractionSkipCount`/`LastConsumedFrameAge`
counters onto the last extraction stats each frame.

The renderer retains one stable `RuntimeRenderSnapshotBatch` copy per pool slot.
`SubmitRuntimeSnapshots(..., storageSlot)` clears and rewrites only the target
slot, while `ExtractRenderWorld(input, storageSlot)` exposes spans from the
slot acquired for rendering. `BeginFrame()` resets transient frame state and the
default read slot but does **not** clear retained runtime snapshot storage,
which prevents extraction-N from overwriting the data render-N is consuming from
N-1. The `integration;runtime;graphics` CPU/null proof for `GRAPHICS-036D`
exercises five frames with `RenderConfig::SynchronousExtraction = false` and
asserts that render-N sees snapshot fields from N-1, with no stalls/skips and
frame age 1 after the bootstrap frame.

The synchronous default remains behavior-preserving (no stalls/skips, frame age
0); this slice does not flip production default behavior. The pool never imports
graphics/ECS/platform — it manages only slot indices and atomics; the consumer
(`graphics/renderer`) still sees only the `const` snapshot through the
`SubmitRuntimeSnapshots`/`ExtractRenderWorld` seam, so no new dependency edge is
introduced.

## Streaming integration

`Extrinsic.Runtime.StreamingExecutor` is the primary persistent async streaming
execution path. `Engine` still carries a temporary compatibility bridge for
legacy `GetStreamingGraph()` producers while migration is in progress; the
bridge's removal is owned by `RUNTIME-105`
(`tasks/backlog/runtime/RUNTIME-105-remove-streaming-graph-bridge.md`).

Shutdown order requirement:

1. `StreamingExecutor::ShutdownAndDrain()`
2. task scheduler teardown

## Stable entity lookup ownership and policy

`Extrinsic.Runtime.StableEntityLookup` is the runtime-owned home for any
`StableId -> entt::entity` resolution. `HARDEN-068` Decision 3 deliberately kept
ECS owning only the `StableId` value type, sentinel, validity check, equality,
ordering, and hasher, and deferred every lookup map to a runtime consumer so the
`ecs -> core` boundary and the "no registry-wide lookup field" rule stay intact.
Graphics never resolves stable ids; it only consumes the render id mirrored
into snapshots. The render id is `static_cast<std::uint32_t>(entt::entity) + 1`
(`StableEntityLookup::ToRenderId`, BUG-026): render id `0` is reserved for the
GPU picking background sentinel (the selection-ID targets clear to 0), so the
first entity of a fresh registry — whose raw handle casts to 0 — stays
pickable, and `entt::null` wraps to the same `0` "no entity" value.

Ownership rules:

- ECS holds **no** lookup map. The sidecar lives only in `runtime`.
- Graphics holds **no** stable-id resolution. It reports render ids; runtime
  resolves them.
- The render/extraction stable id is a reversible encoding of the live handle
  (index + version), so render-id resolution decodes and validates against the
  registry rather than storing a parallel container. The `StableId` map is the
  only materialised state, because a durable id is independent of the volatile
  handle.

Duplicate / stale policy:

- **Duplicate `StableId`:** keep a single deterministic winner — the live entity
  with the smallest `ToRenderId` (entt value) wins, independent of insertion or
  registry-iteration order, and each duplicate occurrence bumps
  `DuplicateStableIds`. Losing duplicates are not retained; a `Rebuild`
  re-derives winners, and `Forget` of a winner drops the mapping until the next
  `Rebuild`/`Track`. The sentinel `kInvalidStableId` is never tracked.
- **Stale handle:** a winner whose entity is destroyed without a matching
  `Forget` is detected lazily on resolve (rejected, erased, counted as
  `StaleEntityResolves` + `StaleEntriesPruned`) or in bulk by `PruneStale`.
- **Missing id:** a resolve for an untracked or sentinel id returns `nullopt`
  and bumps `MissingStableIdLookups`.
- **Reassignment:** when an entity's `StableId` component is replaced and the
  sidecar is updated incrementally with `Track` (hot-reload / undo / editor
  reassignment, rather than a full `Rebuild`), `Track` first drops the entity's
  prior winner entry so the old durable id stops resolving to it and a later
  `Forget` does not leak a stale entry.

Frame wiring (`RUNTIME-092` Slice B): `Engine` owns a `StableEntityLookup`,
attaches it to the `SelectionController` in `Initialize()`
(`SetStableEntityLookup`), and calls `Rebuild(scene)` once per frame in
`RunFrame` immediately before the pick-readback drain so durable-id resolution
and the editor/serialization-facing `ResolveByStableId`/`ResolveSelected` APIs
observe the current frame's entity set. The controller's render-id resolution
seam (`ConsumeHit`, `SetSelectedByStableEntityId`) now routes through the
attached lookup's `ResolveByRenderId` — which decodes the handle *and* validates
it against the live registry — so a pick readback naming a recycled/destroyed
slot is rejected by the single runtime-owned authority (counted as
`StaleEntityHits` on the controller and `StaleEntityResolves` on the lookup)
instead of mis-resolving to the recycled occupant. The seam is opt-in: with no
lookup attached (the controller's standalone unit/contract use) resolution falls
back to the bare `ToEntityHandle` decode plus the controller's own validity
check, so existing direct-drive callers are unaffected. The controller does not
own the lookup's lifetime.

Sandbox default (`RUNTIME-092` Slice B decision): reference-scene entities remain
transient and receive no generated `StableId`; durable ids are emplaced only by
serializer / undo / prefab / editor consumers, matching the `StableId` contract
that transient entities skip the 16-byte cost. The render-id resolution path the
sandbox selection uses needs no `StableId` (it decodes + validates the live
handle), so leaving reference-scene entities transient keeps the default path
allocation-free; durable `StableId` assignment lands with the first serializer /
editor consumer that needs cross-recycle references.

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

`GRAPHICS-034` records the future asset-backed mesh residency bridge that
extends that observation-only seam. Runtime will normalize
`ECS::Components::AssetInstance::Source` through a named
`Runtime::NormalizeAssetInstanceId(...)` helper, store residency in a separate
`Runtime::AssetGeometryCache` keyed by `Assets::AssetId`, and prefer an attached
asset source over procedural / `GeometrySources` residency for the same
renderable. The cache state machine is `NotRequested -> CpuPending ->
GpuUploading -> Ready/Failed`; `Ready` entries bind shared `GpuGeometryHandle`
values through `GpuWorld::SetInstanceGeometry`, generation rebinds acknowledge
GRAPHICS-023D only after a successful replacement bind, and refcount-zero
entries retire through a future asset-geometry maintenance tick adjacent to the
existing `GpuAssetCache::Tick` / geometry-cache ticks. Failed assets use a
visible missing-mesh placeholder plus the GRAPHICS-031 default debug material
once the implementation child lands; until then the bridge is not implemented.

`RenderExtractionCache` also owns the procedural-source residency bridge
(GRAPHICS-030B) and the runtime-authored mesh `GeometrySources` residency bridge
(RUNTIME-085). When a renderable candidate carries
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
`ProceduralGeometryCache::Release(key)`.

When a renderable candidate has no `ProceduralGeometryRef` and no
`AssetInstance::Source` attached, `ExtractAndSubmit()` then builds an
`ECS::Components::GeometrySources::ConstSourceView` for the entity (RUNTIME-085
Slice B). If `BuildConstView` resolves `Domain::Mesh`, the cache routes the view
through `Extrinsic.Runtime.MeshGeometryPacker::PackMesh` against a runtime-owned
`MeshPackBuffer` scratch reused across ticks, calls
`GpuWorld::UploadGeometry(desc)`, records the resulting `GpuGeometryHandle` in a
new sidecar-owned `MeshGeometry` field (distinct from `ProceduralKey` /
`HasSourceAsset`), calls `GpuWorld::SetInstanceGeometry(instance, geometry)`,
and clears the slot's source-asset sentinel. Subsequent clean extractions for the same
entity short-circuit through the `MeshGeometry` handle and increment
`MeshGeometryReuseHits` without re-packing. Subsequent dirty extractions
(`DirtyVertexPositions` / `DirtyFaceTopology` / `DirtyEdgeTopology` / `GpuDirty`
any-of tag set on the entity by an ECS producer) repack the mesh, upload a fresh
`GpuGeometryHandle`, swap the instance binding via `SetInstanceGeometry`,
enqueue the prior handle into the same `framesInFlight` deferred-retire window
the procedural cache uses, drain the dirty tags from the entity, and increment
`MeshGeometryReuploads` + `MeshGeometryReleases` (RUNTIME-085 Slice C). The bridge
is fail-closed: `MeshPackStatus::MissingPositions` and `InvalidTopology` each have
their own counters; every other non-`Success` status
(`MissingHalfedgeTopology`, `MissingFaceTopology`, `EmptyMesh`,
`NonFinitePosition`, `DegenerateAllFaces`, `WrongDomain`) folds into
`MeshGeometryFailedPack`. A failed pack does not bind stale geometry, leaves the
slot's source-asset sentinel cleared, and does not allocate a `GpuGeometryHandle`.
A dirty-reupload pack failure releases the prior residency (fail-closed: the
stale upload is queued for the deferred-retire window, the instance is detached,
and `MeshGeometryReleases` increments) so invalid source data does not keep
rendering the last-good frame; the dirty tags are left set so the entity
re-attempts and uploads fresh once the input recovers.
Mesh-source residency does not share `GpuGeometryHandle`s across entities — each
mesh entity owns its own upload. If a previously-uploaded entity stops selecting
the mesh source on a later frame (it gained `ProceduralGeometryRef` or
`AssetInstance::Source`, or it lost mesh-domain `GeometrySources` topology so
`BuildConstView` no longer resolves `Domain::Mesh`), the cache enqueues the
cached upload into the mesh-residency retire queue, increments
`MeshGeometryReleases`, and — when no other path re-bound the instance this
frame — calls `SetInstanceGeometry(instance, {})` to detach the instance from the
queued (still live, but doomed) slot so the renderer never observes a dangling
binding during the retire window. `RetireMissingRenderables` and `Shutdown` route
the runtime-owned mesh upload through the same queue and increment
`MeshGeometryReleases` (Shutdown then drains the queue inline because hard
teardown collapses the deferred window). When `Release` brings the refcount to
zero the entry is appended to an in-cache retire queue but the underlying
`GpuGeometryHandle` is **not** freed inline; it is freed by
`ProceduralGeometryCache::Tick(currentFrame, framesInFlight, freeFn)` after
`framesInFlight` ticks have elapsed since the release tick, mirroring the
`Graphics::GpuAssetCache::Tick` deferred-retire window (GRAPHICS-030C
Decision 4). `Engine::RunFrame()` drives the procedural cache from the
maintenance phase alongside `GpuAssetCache::Tick` via
`RenderExtractionCache::TickProceduralGeometry(currentFrame, framesInFlight,
renderer)`, which closes over `GpuWorld::FreeGeometry`. The runtime mesh-
residency retire queue is driven from the same maintenance phase by
`RenderExtractionCache::TickMeshGeometry(currentFrame, framesInFlight, renderer)`,
which uses the same anchor-on-first-observation/free-on-deadline semantics; the
per-tick free-count delta surfaces as `MeshGeometryFreeRetires` on the next
`ExtractAndSubmit` call, mirroring `ProceduralGeometryFreeRetires`. If an entity is
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
`ProceduralGeometryRefCountSaturated`. The mesh-source residency bridge adds
`MeshGeometryUploads`, `MeshGeometryReuseHits`, `MeshGeometryReuploads`,
`MeshGeometryFailedPack`, `MeshGeometryMissingPositions`,
`MeshGeometryInvalidTopology`, `MeshGeometryReleases`, and
`MeshGeometryFreeRetires` (RUNTIME-085 Slices B + C). `Uploads` counts first-time
per-entity uploads, `ReuseHits` counts clean-frame rebinds, `Reuploads` counts
dirty-frame repack-and-replace events (a Reupload also increments `Releases`
because the prior handle is queued for retire), and `FreeRetires` is the per-
tick delta of actual `GpuWorld::FreeGeometry` calls fired by `TickMeshGeometry`
once the `framesInFlight` window elapses.

When `BuildConstView` resolves `Domain::Graph` instead (a graph entity carrying
`RenderLines` and/or `RenderPoints`), `ExtractAndSubmit()` routes the view
through `Extrinsic.Runtime.GraphGeometryPacker::PackGraph(view, wantLines,
wantPoints, GraphPackBuffer&)` against a runtime-owned `GraphPackBuffer` scratch,
where `wantLines`/`wantPoints` are derived from the `RenderLines`/`RenderPoints`
hints (RUNTIME-086 Slice B). Node positions form one shared vertex buffer; the
point lane draws it directly and the line lane indexes it via validated
`(e:v0, e:v1)` pairs, so a graph entity owns exactly one `GpuGeometryHandle`
recorded in a sidecar-owned `GraphGeometry` field (distinct from `MeshGeometry`;
mesh and graph domains are mutually exclusive per entity). Clean re-extractions
hit `GraphGeometryReuseHits`; dirty re-extractions (`DirtyVertexPositions` /
`DirtyVertexAttributes` / `DirtyEdgeTopology` / `GpuDirty` any-of tag set on the
entity) repack, upload a fresh handle, enqueue the prior handle into a
graph-residency deferred-retire queue, drain the dirty tags, and increment
`GraphGeometryReuploads` + `GraphGeometryReleases` (RUNTIME-086 Slice C). The
sidecar also records the render-lane mask (`RenderLines` / `RenderPoints`) the
resident upload was packed for; a change in requested lanes — e.g. a points-only
graph that later gains `RenderLines` — repacks through the same reupload path
even when no geometry dirty tag is set, because the line lane's presence changes
the packed line indices. The
bridge is fail-closed: `GraphPackStatus::MissingNodes`/`EmptyGraph` fold into
`GraphGeometryMissingNodes`, `InvalidEdge` into `GraphGeometryInvalidEdges`, and
every other non-`Success` status (`WrongDomain`, `NoRenderLane`,
`MissingEdgeTopology`, `NonFinitePosition`) into `GraphGeometryFailedPack`; a
first-attempt failed pack binds no stale geometry, and a dirty-reupload failure
releases the prior residency (queued for the deferred-retire window, instance
detached, `GraphGeometryReleases` incremented) so invalid node data does not keep
rendering, while the dirty tags stay set for later recovery. Eligibility flips
(the entity gains a procedural/asset source or loses graph-domain topology),
`RetireMissingRenderables`,
and `Shutdown` route the graph upload through the same deferred-retire window,
incrementing `GraphGeometryReleases`; the per-tick free-count delta surfaces as
`GraphGeometryFreeRetires` on the next `ExtractAndSubmit`. The retire queue is
driven from the maintenance phase by `RenderExtractionCache::TickGraphGeometry`,
mirroring `TickMeshGeometry`. The graph counter fields on
`RuntimeRenderExtractionStats` are `GraphGeometryUploads`,
`GraphGeometryReuseHits`, `GraphGeometryReuploads`, `GraphGeometryFailedPack`,
`GraphGeometryMissingNodes`, `GraphGeometryInvalidEdges`, `GraphGeometryReleases`,
and `GraphGeometryFreeRetires` (RUNTIME-086 Slices B + C).

When `BuildConstView` resolves `Domain::PointCloud` (a vertices-only entity, no
edge/halfedge/face/node topology) **and** the entity carries `RenderPoints`,
`ExtractAndSubmit()` routes the view through
`Extrinsic.Runtime.PointCloudGeometryPacker::PackCloud(view, PointCloudPackBuffer&)`
against a runtime-owned scratch buffer (RUNTIME-087). A point cloud is only a
renderable through the `RenderPoints` hint — `RenderSurface`/`RenderLines` have
no faces/edges to draw from a cloud — so a point-cloud-domain entity without
`RenderPoints` is not bound (and any prior point-cloud residency is released by
the eligibility-flip path), which is why a mesh that loses its topology back to
a bare vertex set is not silently re-bound as points. Point positions form the
vertex buffer (no index buffer), so a cloud entity owns exactly one
`GpuGeometryHandle` recorded in a sidecar-owned `PointCloudGeometry` field
(distinct from `MeshGeometry`/`GraphGeometry`; the three domains are mutually
exclusive per entity). Only a uniform world-space radius (the `float`
alternative of `RenderPoints::SizeSource`) is supported in this slice; a
per-point size buffer (the `std::string` alternative) requires a per-point
radius upload that is not implemented here and fails closed into
`PointCloudGeometryFailedPack` rather than uploading mis-sized geometry. Clean
re-extractions hit `PointCloudGeometryReuseHits`; dirty re-extractions
(`DirtyVertexPositions` / `DirtyVertexAttributes` / `GpuDirty` any-of tag set on
the entity — a cloud has no edge/face topology) repack, upload a fresh handle,
enqueue the prior handle into a point-cloud-residency deferred-retire queue,
drain the dirty tags, and increment `PointCloudGeometryReuploads` +
`PointCloudGeometryReleases`. The bridge is fail-closed:
`PointCloudPackStatus::MissingPositions`/`EmptyCloud` fold into
`PointCloudGeometryMissingPositions`, `NonFinitePosition` into
`PointCloudGeometryInvalidPoints`, and `WrongDomain` plus the unsupported
size-source variant into `PointCloudGeometryFailedPack`; a first-attempt failed
pack binds no stale geometry, and a dirty-reupload failure — or a resident cloud
switching to an unsupported per-point size source — releases the prior residency
(queued for the deferred-retire window, instance detached,
`PointCloudGeometryReleases` incremented) so invalid point data does not keep
rendering, while the dirty tags stay set for later recovery. Eligibility flips
(the entity gains a procedural/asset source, loses
point-cloud-domain topology, drops `RenderPoints`, or flips to mesh/graph
domain), `RetireMissingRenderables`, and `Shutdown` route the point-cloud upload
through the same deferred-retire window, incrementing
`PointCloudGeometryReleases`; the per-tick free-count delta surfaces as
`PointCloudGeometryFreeRetires` on the next `ExtractAndSubmit`. The retire queue
is driven from the maintenance phase by
`RenderExtractionCache::TickPointCloudGeometry`, mirroring `TickGraphGeometry`.
The point-cloud counter fields on `RuntimeRenderExtractionStats` are
`PointCloudGeometryUploads`, `PointCloudGeometryReuseHits`,
`PointCloudGeometryReuploads`, `PointCloudGeometryFailedPack`,
`PointCloudGeometryMissingPositions`, `PointCloudGeometryInvalidPoints`,
`PointCloudGeometryReleases`, and `PointCloudGeometryFreeRetires` (RUNTIME-087).

On top of the surface-mesh residency bridge, a mesh entity can opt into optional
**edge** and/or **vertex** primitive views (RUNTIME-088 Slice B). The toggle lives
in the cache-owned `MeshPrimitiveViewSettings` (`EnableEdgeView` /
`EnableVertexView`), set through
`RenderExtractionCache::SetMeshPrimitiveViewSettings(stableId, settings)` /
`ClearMeshPrimitiveViewSettings(stableId)` / `GetMeshPrimitiveViewSettings(stableId)`
from runtime/editor state — the flags never live in ECS components and never carry
graphics handles. The selected model is the **runtime-sidecar** alternative from
the task's deferred decision: each enabled view is derived from the *same*
authoritative mesh `GeometrySources` via
`Extrinsic.Runtime.MeshPrimitiveViewPacker::PackMeshEdgeView` /
`PackMeshVertexView` against one shared cache-owned `MeshPrimitiveViewBuffer`
scratch, uploaded to its **own** `GpuWorld` instance + `GpuGeometryHandle` recorded
in the parent mesh's sidecar (`MeshEdgeViewInstance`/`MeshEdgeViewGeometry`,
`MeshVertexViewInstance`/`MeshVertexViewGeometry`), and re-submitted to
`m_Transforms` every frame as an extra `GpuRender_Line | GpuRender_Unlit`
(edge) or `GpuRender_Point | GpuRender_Unlit` (vertex) lane carrying the parent
surface's transform/bounds/material slot. Faces, edges, and vertices therefore
render as three independent retained renderables over a single mesh data source,
with no ECS storage of graphics handles and no mesh-topology traversal pushed into
`src/graphics/*`. Views are reconciled only while the parent surface is resident
(`MeshGeometry.IsValid()` after `BindMeshGeometry`, so a fail-closed surface pack
keeps its prior upload and the views follow it); a resident-and-clean frame hits
`Mesh{Edge,Vertex}ViewReuseHits`. The views repack on the *same* coalesced mesh
dirty signal the surface uses (`GpuDirty` / `DirtyVertexPositions` /
`DirtyFaceTopology` / `DirtyEdgeTopology`, snapshotted before `BindMeshGeometry`
drains the tags), so a vertex-position edit updates both views' geometry and an
edge-topology edit updates the edge view's line indices; a repack enqueues the
prior view handle into a shared mesh-primitive-view deferred-retire queue and
increments `Mesh{Edge,Vertex}ViewReuploads` + `Mesh{Edge,Vertex}ViewReleases`. The
bridge is fail-closed: the edge view folds `MissingPositions`/`EmptyMesh` into
`MeshEdgeViewMissingPositions`, reports `MissingEdgeTopology` and out-of-range
endpoints in `MeshEdgeViewMissingEdgeTopology` / `MeshEdgeViewInvalidEdges`, and
folds `WrongDomain`/`NonFinitePosition` into `MeshEdgeViewFailedPack`; the vertex
view (no edge topology) uses only `MeshVertexViewMissingPositions` and
`MeshVertexViewFailedPack`. A failed view pack drops just that view (its instance
freed, its geometry queued for retire) without disturbing the surface mesh or the
other view, and reappears on a later dirty frame once the source recovers.
Disabling a view, the parent flipping away from a resident mesh (procedural/asset
take-over or a domain change), `RetireMissingRenderables` (which also erases the
entity's view settings), and `Shutdown` all release the view sidecars — geometry
through the deferred-retire window, the view instance freed immediately. Edge and
vertex view handles share one retire queue driven from the maintenance phase by
`RenderExtractionCache::TickMeshPrimitiveViewGeometry`, mirroring
`TickMeshGeometry`; the per-tick free-count delta surfaces as the shared
`MeshPrimitiveViewFreeRetires`. The mesh-primitive-view counter fields on
`RuntimeRenderExtractionStats` are `MeshEdgeViewUploads`,
`MeshEdgeViewReuseHits`, `MeshEdgeViewReuploads`, `MeshEdgeViewReleases`,
`MeshEdgeViewFailedPack`, `MeshEdgeViewMissingPositions`,
`MeshEdgeViewMissingEdgeTopology`, `MeshEdgeViewInvalidEdges`,
`MeshVertexViewUploads`, `MeshVertexViewReuseHits`, `MeshVertexViewReuploads`,
`MeshVertexViewReleases`, `MeshVertexViewFailedPack`,
`MeshVertexViewMissingPositions`, and `MeshPrimitiveViewFreeRetires`
(RUNTIME-088 Slice B). `Operational` visual proof of the three lanes is owned by
the final working-sandbox acceptance task (RUNTIME-095).

`FindRenderableSidecarForTest(stableId)` returns a `RenderableSidecarView`
exposing the per-entity `Instance`, currently bound `Geometry`,
`ProceduralKey`, source-asset and geometry-slot metadata, the mesh-
residency fields (`MeshGeometry` handle + `HasMeshResidency` flag), the
graph-residency fields (`GraphGeometry` handle + `HasGraphResidency` flag), the
point-cloud-residency fields (`PointCloudGeometry` handle +
`HasPointCloudResidency` flag), and the mesh-primitive-view sidecar fields
(`MeshEdgeViewInstance`/`MeshEdgeViewGeometry` + `HasMeshEdgeView`,
`MeshVertexViewInstance`/`MeshVertexViewGeometry` + `HasMeshVertexView`) so
`contract;runtime` mesh-, graph-, point-cloud-, and mesh-primitive-view-extraction
tests can confirm the residency path picked the right slot without exposing the
private sidecar layout.
`GetProceduralGeometryCacheForTest()` is a read-only test seam used by the
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

The default editor controls are deliberately simple and backend-neutral:
right- or middle-mouse drag rotates orbit/fly/free-look cameras, `WASD` moves or
pans according to the active controller, `Shift` accelerates movement, and mouse
wheel zooms orbit/top-down cameras. The sandbox `Camera / Render` window mirrors
these bindings so controller replacement buttons are not the only visible UI.
Viewport left-click selection is routed from the runtime input context into `SelectionController::RequestClickPick(...)`; it is suppressed while ImGui or an active gizmo owns the mouse. The production input context is updated by the active `Platform::IWindow` backend before runtime selection routing runs.

Camera-controller registration and replacement are one-shot camera-transition
events. `CameraControllerRegistry` marks the slot as pending, and
`Engine::RunFrame()` consumes that bit into
`CameraViewInput::ExplicitCameraTransition` on the first extracted frame after
the change. Graphics uses the immutable flag, plus its own consecutive-camera
delta thresholds, to skip stale previous-frame HZB occlusion without reading
live runtime or ECS state.

The reference-scene seed is still finalized with `BuildReferenceCameraViewInput`
before seeding so its projection keeps the Vulkan clip-space Y inversion used by
the legacy `Graphics::CameraComponent` update at
`src/legacy/Graphics/Graphics.Camera.cpp:34-39`. After seeding, controller state
is authoritative and graphics receives only immutable `CameraViewInput`.

Known gaps relative to legacy and planned camera work are tracked in
`tasks/done/RUNTIME-081A-camera-legacy-gap-analysis.md`: editor-specific camera
shortcuts and any policy that renders multiple camera outputs in one frame remain
outside this runtime-controller surface. Transform-gizmo hit testing, translate/rotate/scale drag application, undo
emission, default input binding, and extraction packet submission now live in
`Extrinsic.Runtime.GizmoInteraction` and the `Engine` wiring completed by
`RUNTIME-084`. Graphics consumes only copied `TransformGizmoRenderPacket` spans.

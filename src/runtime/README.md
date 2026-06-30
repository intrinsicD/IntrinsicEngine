# Runtime

`src/runtime` is the composition root for the engine. It owns
subsystem instantiation order, frame-phase orchestration, and deterministic
startup/shutdown.

## Public module surface

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.Engine` | Composition root, frame loop, subsystem wiring, app-facing reference engine config helper, the runtime-owned `ImportAssetFromPath(...)` / `ReimportAsset(...)` facades that compose promoted ASSETIO geometry/model/texture decoders, `AssetService`, standalone geometry ECS materialization with local/world culling bounds plus one-shot main-camera focus for mesh/graph/point-cloud imports, and model/texture handoffs for editor file/import commands. Platform drop events enqueue geometry decode/conversion on `Runtime.StreamingExecutor`; `AssetService` and ECS materialization stay in the main-thread apply phase. Dropped ambiguous geometry extensions such as PLY try supported geometry payloads in import-router order before failing closed. Runtime logs dropped-file receipt, per-path routing/queue decisions, and shared import completion so failed drops remain diagnosable outside the editor panel. Direct mesh imports publish the decoded raw mesh entity with explicit or synthesized `v:normal` before derived materialization; missing or invalid vertex texcoords, UV-atlas resolution, and generated normal texture baking run as downstream `Runtime.StreamingExecutor` work and apply back to the same entity with geometry dirty tags. Current generated direct normal textures still bake through the CPU compatibility helper, request GPU upload when the backend can accept it, and register a data-only normal binding for the extraction-owned material sidecar with `MaterialNormalTextureSpace::ObjectSpaceNormal` metadata. `GRAPHICS-104` owns replacing that CPU bake with the asynchronous GPU object-space normal bake job. Mesh imports that fail strict shared-topology conversion only for renderable non-manifold/inconsistent-winding diagnostics materialize through a disconnected render-only mesh fallback; geometry algorithms still use the strict converter as their topology contract. The `SaveSceneToPath(...)` / `LoadSceneFromPath(...)` / `NewSceneDocument()` / `CloseSceneDocument()` scene-file facades route through one runtime scene-replacement boundary: pre-clear render-extraction sidecar drain, selection hover/click/pick-correlation cleanup, refined-primitive cache reset, scene registry replacement/clear, stable-lookup rebuild or clear, and `EditorCommandHistory` dirty/path transitions. Scene-changing import facades mark the same runtime history dirty; UI reads the history snapshot but does not own document state. |
| `Extrinsic.Runtime.AssetIngestStateMachine` | Runtime-owned ingest request/result state machine (`RUNTIME-101`). Exports request sources for manual import, dropped files, and reimport; phases from `Queued` through route resolution, decode scheduling/execution, main-thread apply, `Complete`, `Failed`, and `Cancelled`; and a diagnostic taxonomy for missing path/file, route failures, invalid reimport target, duplicate active request, decode failure, callback failure, materialization failure, cancellation, stale completion, invalid transition, and unknown handles. The state machine is backend-neutral and owns no decoders, ECS mutation, `AssetService`, graphics, RHI, or worker threads. `Engine::ImportAssetFromPath(...)`, `Engine::ReimportAsset(...)`, synchronous dropped non-geometry imports, and deferred dropped geometry imports submit records through this contract; deferred geometry decode still runs on `Runtime.StreamingExecutor` and completes/fails only from its main-thread apply lane. Reimport resolves the existing asset path and payload kind from `AssetService`, reloads the same `AssetId` transactionally, lets texture/model-scene handoffs consume `Reloaded`/`Ready` events, and does not revive ECS `AssetSourceRef` coupling; standalone geometry scene entities remain authoring snapshots and are not duplicated. |
| `Extrinsic.Runtime.AssetGeometryIO` | Runtime-owned registration seam for `ASSETIO-001` Slice B. Exports `RegisterPromotedGeometryIOCallbacks(Assets::AssetGeometryIOBridge&)`, imports the promoted geometry IO modules in runtime, and registers OBJ/OFF/STL/PLY mesh importers, XYZ/PTS/XYZRGB/PCD/PLY point-cloud importers, TGF/edge-list graph importers, OBJ/STL/PLY mesh exporters, XYZ/PCD/PLY point-cloud exporters, and TGF/edge-list graph exporters. The adapter translates legacy geometry `Core.Error` decoder failures into promoted `Extrinsic.Core.Error` codes before they enter the asset bridge, keeping `src/assets` free of geometry/runtime/graphics imports. It does not construct ECS entities or GPU residency; later `ASSETIO-001` slices own model/texture payloads and runtime handoff. |
| `Extrinsic.Runtime.AssetMeshNormals` | Runtime-owned mesh payload materialization helper for asset import paths. Exports `RuntimeMeshUvResolutionOptions`, `RuntimeMeshGeometryOnlyOptions`, `RuntimeMeshMaterializationDiagnostics`, `BuildRuntimeHalfedgeMeshGeometryOnly(...)`, `BuildRuntimeHalfedgeMeshMaterialization(...)`, the compatibility wrapper `BuildRuntimeHalfedgeMeshWithNormals(...)`, and `MeshPayloadHasValidVertexTexcoords(...)`. The geometry-only helper triangulates promoted `Geometry::MeshIO::MeshIOResult` payloads for immediate authoring publication without invoking UV atlas resolution or texture baking; it copies typed vertex properties and writes explicit `v:normal` vectors or deterministic area-weighted fallback normals so the first upload has count-matched normals. The full materialization helper validates authored UVs through `Geometry.UvAtlas`, preserves valid authored `v:texcoord` by default, invokes the selected atlas backend when UVs are missing/invalid or regeneration is forced, and fails closed under the default required-UV policy when no valid UVs can be produced. It copies explicit `v:normal` vectors or synthesizes deterministic area-weighted normals, preserves typed vertex payload properties such as `glm::vec3`/`glm::vec4` colors and scalar/vector algorithm results across seam-split output using source-vertex xrefs, records `v:source_vertex` / `f:source_face` provenance, and reports authored-vs-generated UV provenance, invalid-authored status, backend status, seam-split count, chart count, and atlas dimensions. Direct mesh imports may opt into the disconnected render-only fallback for non-manifold/inconsistent-winding payloads; model-scene materialization uses the strict topology path. |
| `Extrinsic.Runtime.MeshAttributeTextureBake` | Runtime-owned CPU texture bake helper for mesh attributes over resolved UVs. Exports the generic `MeshAttributeTextureBakeRequest` / `BakeMeshAttributeTexture(...)` seam for `GeometrySources::ConstSourceView` and `Geometry::HalfedgeMesh::Mesh`, stable `BuildMeshAttributeTextureBakeAssetPath(...)` keys, status/result/diagnostic records, and compatibility wrappers `BakeMeshVertexNormalTexture(...)` and `BakeMeshVertexColorTexture(...)`. The generic path supports vertex and face source domains, finite scalar `float`/`double`, label `uint32`, and finite `glm::vec2`/`glm::vec3`/`glm::vec4` properties whose count matches the selected domain. Encoders cover scalar colormap, linear scalar R8, label palette RGBA8, vector2 RG8, vector3 RGB8, normal RGBA8, and RGBA color outputs; scalar ranges are either finite auto-ranges with flat-range expansion or explicit increasing manual ranges. The baker requires mesh-domain topology and count-matched finite vertex `v:texcoord`; missing UVs, unsupported domains/types, invalid ranges/resolutions, non-finite data, degenerate UV triangles, and zero-coverage bakes fail closed with diagnostics. It does not generate UVs and has no `AssetService`, graphics, RHI, or Vulkan dependency; runtime callers can use the stable generated path to reload the intended CPU texture payload when a dirty stamp changes. |
| `Extrinsic.Runtime.ObjectSpaceNormalBakeBinding` | Runtime-owned bridge from a completed GPU object-space normal bake queue entry to extraction material binding. `TryBindReadyObjectSpaceNormalBake(...)` checks that the generated texture `AssetId` has a ready texture view in `Graphics::GpuAssetCache` before consuming the queue completion; pending cache entries leave the queue untouched, stale completions are rejected through `RuntimeObjectSpaceNormalBakeQueue`, and accepted completions install a data-only `MaterialTextureAssetBindings::Normal` binding with `MaterialNormalTextureSpace::ObjectSpaceNormal` on `RenderExtractionCache`. It performs no render-thread command recording, no Vulkan/backend calls, no CPU texture bake fallback, and no live `AssetService` mutation. |
| `Extrinsic.Runtime.ObjectSpaceNormalBakeQueue` | Runtime-owned scheduling contract for GPU object-space normal bake requests. It selects the generated texture `AssetId` from a stable resolved geometry/UV/normal content key when available, falls back to an entity-scoped generated asset, records entity/geometry/UV/normal generation plus resolution/padding/normal-space stale keys, no-ops deterministically when the graphics backend is non-operational without scheduling a CPU fallback, and rejects stale completions before a caller may mutate material bindings. It owns no ECS mutation, render-thread command recording, `GpuAssetCache` residency, Vulkan handles, or live `AssetService`; those remain Engine/render-thread integration work under `GRAPHICS-104`/`RUNTIME-129`. |
| `Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission` | Runtime-owned GPU submission bridge for queued object-space normal bake work. `BeginObjectSpaceNormalBakeGpuSubmission(...)` validates a queue submission against a graphics `ObjectSpaceNormalTextureBakePlan`, rejects stale plan/completion mismatches and currently-unavailable padded dilation plans before cache allocation, registers the generated texture as a GPU-produced pending texture in `GpuAssetCache`, and returns an `ObjectSpaceNormalTextureBakeGpuRecordDesc` for render-thread command recording. `MarkObjectSpaceNormalBakeGpuSubmissionReady(...)` attaches the submitted-frame ready value to the cache entry; material binding remains deferred to `Runtime.ObjectSpaceNormalBakeBinding` after `GpuAssetCache` promotes the texture to `Ready`. It does not own Engine import scheduling, command-list submission, CPU texture fallback, or live `AssetService` mutation. |
| `Extrinsic.Runtime.SelectedMeshTextureBake` | Runtime-owned selected-mesh texture bake command surface (`RUNTIME-115`). Exports selected-entity bake request/result/status DTOs, validates stable entity liveness, mesh `GeometrySources`, vertex/face source property compatibility, resolved finite `v:texcoord`, output resolution/range, and progressive target slot compatibility, then routes successful bakes through `MeshAttributeTextureBake` and `AssetService` generated texture payload loads/reloads. Synchronous command execution can bind the generated texture through `EditorCommandHistory`; derived-job execution records observable CPU bake work through `DerivedJobRegistry`, preserves previous outputs while pending/failed, and discards stale apply results before mutating bindings. It owns no UI widgets, graphics resources, RHI/Vulkan objects, or GPU upload state. |
| `Extrinsic.Runtime.ProgressiveRenderData` | Data-only progressive render-data contract accepted by ADR-0021 and implemented by `RUNTIME-111`. Exports entity shape, geometry-domain, lane, presentation-kind, slot semantic/source/readiness, generated-output policy/provenance, job-domain metadata, `ProgressivePresentationBindings`, property binding descriptors, property compatibility resolution, and compatible-first property option enumeration. Descriptors persist asset ids, names, value kinds, counts, defaults, readiness, and generated-output policy; they never persist raw property pointers, borrowed property views, transient worker/job state, graphics handles, RHI/Vulkan objects, or live `AssetService` ownership. |
| `Extrinsic.Runtime.RenderArtifactPublication` | Runtime-owned render artifact publication contract (`RUNTIME-127`). Exports an artifact registry keyed by renderer id, snapshot id, view/output recipe id, source revisions, and output purpose; lifecycle kinds for transient frames, cached frames, saved files, preview-only outputs, dataset/batch outputs, readback/metric outputs, and candidate project results; UI-facing states for unpublished, stale, canceled, failed, superseded, published, and applied artifacts; explicit provenance-carrying publish/apply/undo commands; and an audit log. Registration never mutates project data. Applying a candidate artifact authorizes a project mutation for the caller-owned command path and records undo/audit metadata, but the registry itself does not import UI, renderer backends, ECS mutation callbacks, or project persistence. |
| `Extrinsic.Runtime.MethodFigureExport` | Runtime-owned method figure-data export seam (`RUNTIME-133`). Exports copied data records for metric series, scalar summaries, run manifests, and point samples, plus deterministic CSV/JSON/PLY serializers and atomic file writers. It is CPU/headless only: methods and geometry compute numeric arrays, while runtime owns text serialization, manifest metadata, path validation, and fail-closed write diagnostics. |
| `Extrinsic.Runtime.GeometryAvailability` | Runtime-owned geometry availability resolver (`RUNTIME-117`). Exports CPU source/provenance queries, property-domain support, element counts, and `Surface`/`Edges`/`Points` render-lane readiness from ECS `GeometrySources` plus promoted `RenderSurface`, `RenderEdges`, and `RenderPoints` components. Runtime extraction, progressive property resolution, editor command preflight, and `SandboxEditorUi` consume this resolver so mesh vertices, graph nodes, and point-cloud points can satisfy point-lane consumers without using exact `GeometrySources::ActiveDomain()` as the common capability gate. |
| `Extrinsic.Runtime.ProgressivePresentationExtraction` | Descriptor-to-snapshot resolver for progressive presentation state (`RUNTIME-113`). Exports `BuildProgressivePresentationSnapshot(...)`, slot extraction records, and stats for defaults, pending slots, ready textures, ready property buffers, unsupported slots, diagnostics, and previous-output retention. Mesh surface slots can resolve uniform defaults, authored/generated texture assets, property-bake pending/ready state, and unsupported direct property-buffer diagnostics; graph vertex/edge and point-cloud domains resolve direct property-buffer descriptors independently so vertex/point and edge/line presentations can carry separate colors, scalar fields, sizes, widths, or normal/orientation data. |
| `Extrinsic.Runtime.DerivedJobGraph` | Runtime-owned derived-work scheduler over `Runtime.StreamingExecutor` (`RUNTIME-112`). Exports stable `DerivedJobKey`/`DerivedJobRecord` vocabulary, CPU job submission, explicit dependencies, follow-up scheduling, per-entity/global snapshots, cancellation, stale apply validation, previous-output retention, main-thread apply, non-blocking readback parking/resume for `RUNTIME-126`, readback diagnostics, and deterministic diagnostics for unsupported `GpuCompute`, `GpuGraphics`, and unresolved `Auto` domains. The registry owns no texture uploads, graphics resources, or persisted scene state; GPU-capable domains are metadata until a concrete backend task provides an implementation. |
| `Extrinsic.Runtime.GpuReadbackJob` | Runtime-owned GPU-result write-back helper (`RUNTIME-126`). Exports backend-neutral property-binding validation and submission helpers that compose `DerivedJobRegistry`, `Runtime.StreamingExecutor`, GRAPHICS-096 async readback delivery, GRAPHICS-098 `GpuTransfer`, and GRAPHICS-095 buffer-dimension checks. A submitted readback job issues a graphics public API readback, parks in `WaitingForReadback`, resumes only after `DrainReadbacks()` observes delivered bytes, and writes the result into a count-matched `Geometry::PropertyRegistry` property on the main-thread apply lane. Callers that own a derived-job registry must drain transfer completions first, then call `DerivedJobRegistry::DrainReadbacks()`, then `ApplyMainThreadResults()` so follow-up jobs remain blocked until the write-back has applied. Runtime does not import Vulkan or graphics backend internals for this path; follow-up CPU->GPU visibility continues through the existing property binding/upload surfaces rather than a special readback upload lane. See ADR-0023 and the RUNTIME-112 / GRAPHICS-096 / GRAPHICS-098 task lineage for the reuse-not-reinvent contract. |
| `Extrinsic.Runtime.KMeansBackend` | Runtime-owned RHI-visible KMeans backend seam (`GEOM-052`). Exports `ClusterKMeans(...)` overloads that accept `Extrinsic::RHI::IDevice&`, evaluate `IDevice::IsOperational()` for `Geometry::KMeans::Backend::GPU` requests, and fall back to the geometry CPU reference while preserving `RequestedBackend`, `ActualBackend`, and `FellBackToCPU` telemetry. No GPU kernel is implemented here; a real Vulkan-compute KMeans backend must be a later parity-gated task. |
| `Extrinsic.Runtime.AssetModelTextureIO` | Runtime-owned registration seam for `ASSETIO-001` Slice C.2b. Exports `RegisterPromotedModelTextureIOCallbacks(Assets::AssetModelTextureIOBridge&)`, imports tinygltf/stb and promoted geometry IO privately in runtime, registers GLTF/GLB model-scene decoders and PNG/JPEG/TGA/BMP/HDR texture decoders, and maps decoded mesh primitives, embedded images, material texture references, and external-resource diagnostics into the promoted asset payload records. `src/assets` owns route resolution, primary/external byte transport, callback dispatch, and payload validation; runtime owns concrete decoder dependencies. Texture GPU residency flows through `Extrinsic.Runtime.AssetModelTextureHandoff`; model-scene ECS/material records flow through `Extrinsic.Runtime.AssetModelSceneHandoff`. |
| `Extrinsic.Runtime.SceneSerialization` | Backend-neutral scene document seam (`RUNTIME-098`, hardened by `RUNTIME-100`). Exports JSON save/load helpers over `ECS::Scene::Registry` plus `Core::IO::IIOBackend`, result/stat records, and fail-closed diagnostics. The current promoted document persists metadata names, durable stable ids, local transforms, hierarchy parent links, selectable tags, render geometry hints, visualization configs and lane overrides, progressive presentation bindings/generated-output policy, and mesh/graph/point-cloud `GeometrySources` property data for sandbox-authored entities, including mesh-domain `v:position`, `v:normal`, and `v:texcoord` where present. Unsupported persistence families are counted deterministically in `SceneSerializationStats` (`Unsupported*Entities`) instead of being silently treated as supported. It deliberately omits renderer/RHI caches, GPU handles, dirty-tracker UX, file dialogs, transient progressive job snapshots/readiness execution state, borrowed property views, arbitrary legacy asset source reimport, renderer/runtime visualization adapter bindings, and arbitrary component persistence. |
| `Extrinsic.Runtime.AssetModelTextureHandoff` | Runtime-owned texture residency seam for `ASSETIO-001` Slice D.1. Exports `BuildGpuTextureDesc(...)`, `RequestTextureAssetUpload(...)`, diagnostics/options records, and `AssetModelTextureHandoff`, which subscribes to `AssetService::Ready` events, reads promoted `AssetTexture2DPayload` records, maps supported CPU texture formats to RHI texture descriptors, and submits `Graphics::GpuAssetCache::RequestUpload(GpuTextureRequest)` without importing graphics/RHI into `src/assets`. RGB8 and unknown CPU texture formats fail closed as `AssetUnsupportedFormat` and can mark the GPU cache entry failed. `DeviceNotOperational` and in-flight upload conflicts are recorded as retryable upload deferrals so CPU imports can succeed under the Null backend. `RequestTextureAssetUpload(...)` is idempotent for cache entries already `GpuUploading` or `Ready`, so embedded child textures requested by the model-scene handoff do not double-submit when their own texture `Ready` event is later observed. |
| `Extrinsic.Runtime.AssetModelSceneHandoff` | Runtime-owned model-scene materialization seam for `ASSETIO-001` Slice D.2, extended by `RUNTIME-114`. Exports deterministic embedded-texture child-path helpers (`<model-path>.embedded-texture-<image-index>.<ext>`), generated-texture child-path helpers, `LoadEmbeddedTextureAsset(...)`, `LoadGeneratedTextureAsset(...)`, `MaterializeModelSceneAsset(...)`, diagnostics/record types, and `AssetModelSceneHandoff`, which subscribes to model-scene `AssetService::Ready` events. The handoff reads promoted `AssetModelScenePayload` records, mints child `AssetTexture2DPayload` assets keyed by stable synthetic paths, optionally requests their GPU uploads through `AssetModelTextureHandoff`, converts mesh primitive payloads through `Extrinsic.Runtime.AssetMeshNormals` into ECS `GeometrySources` mesh domains with explicit or synthesized `v:normal`, authored-preserved or generated-atlas `v:texcoord`, plus preserved typed vertex properties, creates default ECS entities with `RenderSurface`, and creates `MaterialSystem` leases plus `MaterialTextureAssetBindings` keyed by child `AssetId`s. When `ProgressiveRawGeometryFirst` is enabled, mesh primitives publish from geometry-only decoded payloads with explicit or synthesized `v:normal` before UV atlas or texture-bake work completes, receive `ProgressivePresentationBindings` with albedo/normal/roughness/metallic surface slots, and enqueue observable UV atlas, normal-bake, and compatible albedo/property-color bake jobs through an injected `DerivedJobRegistry`; the CPU-contract bake jobs update generated texture descriptors on the main thread and deliberately do not allocate RHI/Vulkan resources. When configured outside that progressive mode, materials without authored normal textures can receive generated normal textures baked from a named `glm::vec3` property (default `v:normal`), and materials without authored base-color textures can receive generated albedo textures baked from a named `glm::vec3`/`glm::vec4` property (default `v:color`); both generated paths run after UV materialization and only require resolved valid `v:texcoord`, so meshes that omitted source UVs can bake from the generated atlas. Generated normal bindings are marked as `MaterialNormalTextureSpace::ObjectSpaceNormal`; authored normal textures keep the tangent-space default and are not consumed by the object-space shader flag. Generated textures route through the same child asset/upload/material-binding path as embedded textures, while diagnostics count authored UV primitives, generated-atlas primitives, invalid-authored UVs, backend failures, seam-split vertices, progressive raw publications, progressive binding creation, queued UV/normal/bake jobs, and last atlas chart/dimension records. Generated entities intentionally stay on the existing `GeometrySources` residency lane and do not stamp `AssetInstance::Source`, because the current extraction contract treats `AssetInstance::Source` as the alternative asset-cache observation path and suppresses `GeometrySources` mesh residency. Imported primitives that carry no authored material (e.g. plain OBJ/PLY/STL, or a glTF primitive with no material index) bind a lazily-created, handoff-scoped neutral lit `StandardPBR` default material so they shade using their vertex normals instead of falling back to the unlit `DefaultDebugSurface` (slot 0); slot 0 stays reserved for genuine missing/invalid bindings (`GRAPHICS-031`), and `DefaultLitMaterialInstancesCreated`/`MaterialLessPrimitivesAssignedDefaultLit` diagnostics count the default-lit path. Material records preserve the resolved material slots and `AssetId` texture bindings; authored textures take priority over generated fallback textures, bindings defer while child texture assets are pending or the headless GPU backend is non-operational, and `ResolvePendingMaterialTextureBindings()` re-resolves after upload or reload without rerunning model import. Broader file-backed GPU readback remains opt-in `Operational` work such as `RUNTIME-095` or a future value-gated task. |
| `Extrinsic.Runtime.EcsSystemBundle` | Runtime-owned activation helper for the promoted baseline ECS systems. Exports `PromotedEcsSystemBundleStats` and `RegisterPromotedEcsSystemBundle(FrameGraph&, ECS::Scene::Registry&)`, which adds `Extrinsic.ECS.System.TransformHierarchy` and `Extrinsic.ECS.System.BoundsPropagation` as FrameGraph passes. `Engine::RunFrame()` invokes the helper inside the fixed-step substep loop after `IApplication::OnSimTick` and before `Core::FrameGraph::Compile`, so dirty world matrices and bounds are refreshed every substep before render extraction observes them (`RUNTIME-091`). Also exports `PreRenderTransformFlushStats` and `FlushPreRenderTransformState(ECS::Scene::Registry&)` (BUG-024): a direct `TransformHierarchy` → `BoundsPropagation` → `RenderSync` pass run by `Engine::RunFrame()` after the variable tick, ImGui editor hook, and gizmo-interaction drive — and before transform-gizmo packet build and render extraction — so post-fixed-step UI/editor/gizmo transform edits reach the rendered model matrix in the same frame. |
| `Extrinsic.Runtime.EditorCommandHistory` | Runtime/editor-owned undo/redo and document dirty-state seam (`RUNTIME-102`). Exports `EditorCommandHistory`, deterministic result/status/snapshot DTOs, typed adapters for transform edits, single-selection replacement, legacy mesh primitive-view compatibility settings, visualization configs, spatial-debug bindings, compound commands with rollback, and a hierarchy delete/orphan planning helper. The history stores labels, capacity-bounds undo/redo stacks, active scene path, revision/saved-revision dirty tracking, and fail-closed stale/missing dependency statuses. ECS remains data-authoritative; the service lives in runtime because editor command policy, sidecars, dirty-state UX, and recursive hierarchy policy are above ECS. Scene create/duplicate/delete materialization and asset-import undo stay deferred until runtime scene lifecycle/snapshot support can make them reversible without serializing renderer/RHI state. |
| `Extrinsic.Runtime.PhysicsBridge` | Runtime-owned ECS-to-physics bridge added by `PHYSICS-001`. Exports `PhysicsBridgeFixedStepConfig`, `PhysicsBridgeDiagnostics`, and `PhysicsBridge`, which owns an `Extrinsic.Physics.World`, a `StableId -> BodyHandle` sidecar, descriptor synchronization from ECS collider/rigid-body authoring, fixed-step accumulator stepping, and dynamic-body transform writeback with `Transform::IsDirtyTag` / `Transform::WorldUpdatedTag` stamping. The bridge keeps handles out of ECS, skips static/kinematic writeback with diagnostics, and deliberately does not route contact events until `PHYSICS-002` exposes contact records. |
| `Extrinsic.Runtime.CameraControllers` | Runtime-owned camera controller surface. Exports `CameraFocusTarget`, `ICameraController`, `OrbitCameraController`, `FlyCameraController`, `FreeLookCameraController`, `TopDownCameraController`, `CreateCameraController()`, `CameraControllerSlot`, and `CameraControllerRegistry`. `ICameraController::Focus(...)` performs one-shot centering/framing of imported or selected geometry without making UI own camera state. Controllers consume `Extrinsic.Platform.Input::Context`, use `Core::Extent2D` for viewport dimensions, and produce immutable `Graphics::CameraViewInput` for renderer extraction. `TopDownCameraController` seeds from the input view focus point, not the input position XZ, so the default reference triangle remains centered when starting in or switching to top-down mode. GRAPHICS-040A keeps the base `CameraViewInput` ABI stable; graphics-side temporal jitter is selected through `BuildTemporalCameraViewSnapshot(...)`, which accepts the rendered-frame index explicitly, while GRAPHICS-040C maps the renderer AA selector to TAA/external reconstruction without adding runtime camera authority. The registry exposes named slots (`Main`, `Preview`, `TopDown`, `EditorSecondary`) while `Engine::RunFrame()` currently drives the `Main` slot; UI-001 Slice C exposes the engine-owned registry through `Engine::GetCameraControllerRegistry()` so editor commands can replace slots without storing camera authority in UI state. |
| `Extrinsic.Runtime.ProceduralGeometry` | Procedural-geometry descriptor surface (`ProceduralGeometryKey`, key hash, `ProceduralGeometryCache` value type with `EnsureResident` / `Release` / `Tick` / `Find`). Reuses the `ProceduralGeometryKind` enum and POD `ProceduralGeometryParams` defined in `Extrinsic.ECS.Component.ProceduralGeometryRef`. `EnsureResident(key, uploadDesc, uploadFn)` either invokes the injected upload functor exactly once on a new key or hits an existing entry and increments a `std::uint32_t` refcount; `Release(key)` decrements and enqueues the entry into a deferred retire queue on the refcount-zero transition; `Tick(currentFrame, framesInFlight, freeFn)` anchors retire deadlines (`currentFrame + framesInFlight`) and calls `freeFn` on entries whose deadline has been reached, mirroring `Graphics::GpuAssetCache::Tick` semantics. Resurrecting a key inside the retire window cancels the queued free and reuses the bit-identical `GpuGeometryHandle`. N entities sharing `(Kind, Hash(Params))` share one `GpuGeometryHandle`. No live ECS, no graphics imports beyond the existing `Extrinsic.Graphics.GpuWorld` value-type edge. This procedural path remains available for explicit procedural fixtures and callers; the default reference/sandbox triangle now uses mesh-domain `GeometrySources`. |
| `Extrinsic.Runtime.ProceduralGeometryPacker` | Per-kind packer `Pack(kind, params, scratch) -> std::optional<GeometryUploadDesc>` consuming a runtime-owned `ProceduralGeometryPackBuffer` reused across ticks. Triangle is the only in-scope packer for Impl-A; the surface vertex layout is `{pos.xyz, uv, normal.xyz}` (32 bytes/vertex) matching the promoted mesh surface layout consumed by surface/depth/face-selection shaders. Cube / Quad / Sphere / LineStrip extend the enum + packer table without cache or extraction lifecycle changes. |
| `Extrinsic.Runtime.MeshGeometryPacker` | Runtime-authored mesh `GeometrySources` → `GpuWorld::GeometryUploadDesc` packer (`RUNTIME-085` Slice A). Exports `MeshVertex` (32-byte `{pos.xyz, uv, normal.xyz}` surface layout matching `ProceduralVertex`), `MeshPackBuffer` (caller-owned scratch reused across ticks), `MeshPackStatus` (`Success`, `WrongDomain`, `MissingPositions`, `MissingHalfedgeTopology`, `MissingFaceTopology`, `EmptyMesh`, `InvalidTopology`, `NonFinitePosition`, `MissingTexcoords`, `NonFiniteTexcoord`, `DegenerateAllFaces`), `MeshPackResult`, `PackMesh(ConstSourceView, MeshPackBuffer&)`, and `DebugNameForMeshPackStatus()`. `PackMesh` validates `v:position`, count-matched finite `v:texcoord`, `h:to_vertex`, `h:next`, `h:face`, and `f:halfedge`; for each face slot it consults `h:face` on the first ring halfedge to decide ownership and skips slots whose ring no longer claims the face — this is required because `ECS::Components::GeometrySources::PopulateFromMesh` writes `f:halfedge` for every face slot via `mesh.Halfedge(fh)`, but `Geometry::HalfedgeMesh::DeleteFace` invalidates only `h:face` on the ring's halfedges, leaving the deleted face's `f:halfedge` pointing at a still-walkable ring that must not be fan-triangulated. For live slots the ring is walked (step-capped at `halfedgeCount` so a malformed `h:next` cycle fails closed rather than spinning) and fan-triangulated from the first ring vertex; a mid-ring halfedge whose `h:face` disagrees with the current face fails closed as `InvalidTopology`. Vertex bytes are written in input order so emitted surface indices index directly into the source `Vertices` PropertySet; `MeshVertex::U/V` is populated only from `v:texcoord`, so missing/count-mismatched texcoords fail as `MissingTexcoords` and non-finite texcoords fail as `NonFiniteTexcoord`. `MeshVertex::Nx/Ny/Nz` is populated from count-matched finite `v:normal` when present and otherwise uses a deterministic +Z default; UV fields are never used for normal encoding. `LocalBounds.LocalSphere` is filled from the local AABB midpoint and half-diagonal so culling/transform sync has a deterministic non-empty local bound; `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites them with the per-frame world transform via `ExtractBounds`. Also exports `BuildSurfaceTriangleFaceMap(ConstSourceView, std::vector<std::uint32_t>&)` (`RUNTIME-093`), the inverse of the surface picking payload: it records the owning face row for every surface triangle in the exact order `PackMesh` emits them (the two share one internal `ProduceFaceRing` walk so they cannot drift), so runtime selection refinement can translate a `gl_PrimitiveID`-based `Face` selection-id payload back to a face row even when n-gon faces fan-triangulate to multiple GPU triangles. |
| `Extrinsic.Runtime.MeshPrimitiveViewPacker` | Runtime-authored derivation of optional mesh *edge* (line-list) and *vertex* (point) render views from the same authoritative mesh `GeometrySources` (`RUNTIME-088` Slice A, component-driven by `RUNTIME-106`). Exports `MeshPrimitiveViewSettings` as a legacy compatibility DTO, `MeshPrimitiveVertex` (20-byte `{pos.xyz, neutral uv}` retained line/point compatibility layout matching `GraphVertex`/`PointCloudVertex`), `MeshPrimitiveViewBuffer` (caller-owned `VertexBytes` + SoA `Channels` + `LineIndices` scratch reused across ticks), `MeshPrimitiveViewStatus` (`Success`, `WrongDomain`, `MissingPositions`, `EmptyMesh`, `MissingEdgeTopology`, `InvalidEdge`, `NonFinitePosition`), `MeshPrimitiveViewResult`, `DebugNameForMeshPrimitiveViewStatus()`, and the two entry points `PackMeshEdgeView(ConstSourceView, MeshPrimitiveViewBuffer&)` / `PackMeshVertexView(ConstSourceView, MeshPrimitiveViewBuffer&)`. Both require mesh source provenance and read mesh vertex positions (`v:position` on `Vertices`) into one shared vertex buffer in input order while publishing explicit position and neutral-texcoord channel spans in `GeometryUploadDesc`. The edge view additionally requires authored `Edges` rows or derivable halfedge/face wire topology, while the vertex view only requires vertex positions and emits no index buffer (the retained point pipeline draws the vertex buffer directly). UV fields are written as neutral zeroes and are never used for normal encoding; dedicated point/surfel normal residency is future work. Deleted vertex/edge rows are packed in place (no compaction), mirroring the graph/point packers. `LocalBounds.LocalSphere` is filled from the vertex AABB midpoint and half-diagonal; `WorldSphere`/`WorldAabb*` stay zero (runtime extraction overwrites them via `ExtractBounds`). Runtime extraction now drives these packers from ECS `RenderEdges` / `RenderPoints` component presence and config; the old `Engine::{Set,Get,Clear}MeshPrimitiveViewSettings` facade translates to those components and is not an extraction authority. |
| `Extrinsic.Runtime.GraphGeometryPacker` | Runtime-authored graph `GeometrySources` → `GpuWorld::GeometryUploadDesc` packer (`RUNTIME-086` Slice A). Exports `GraphVertex` (20-byte `{pos.xyz, neutral uv}` retained line/point layout), `GraphPackBuffer` (caller-owned scratch reused across ticks), `GraphPackStatus` (`Success`, `WrongDomain`, `NoRenderLane`, `MissingNodes`, `EmptyGraph`, `MissingEdgeTopology`, `InvalidEdge`, `NonFinitePosition`), `GraphPackResult`, `PackGraph(ConstSourceView, wantLines, wantPoints, GraphPackBuffer&)`, and `DebugNameForGraphPackStatus()`. `PackGraph` reads node positions (`v:position` on `Nodes`) into one shared vertex buffer in input order so edge endpoints index directly into it; the point lane (`wantPoints`) draws the vertex buffer directly, the line lane (`wantLines`) emits a line-list `LineIndices` of validated `(e:v0, e:v1)` endpoint pairs (a graph with zero edges is valid and yields no line indices). UV fields are written as neutral zeroes and are never used for normal encoding. Deleted node/edge rows are packed in place (no compaction) in this slice. `LocalBounds.LocalSphere` is filled from the node AABB midpoint and half-diagonal; `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites them via `ExtractBounds`. One `GpuGeometryHandle` per graph entity carries both lanes, matching the canonical single-renderable-instance contract. `Runtime.RenderExtraction` integration, `GraphGeometry*` diagnostics counters, eligibility-flip releases, dirty-domain reupload, and the `TickGraphGeometry` deferred-retire window landed in `RUNTIME-086` Slices B + C — see the residency prose below. |
| `Extrinsic.Runtime.PointCloudGeometryPacker` | Runtime-authored point-cloud `GeometrySources` → `GpuWorld::GeometryUploadDesc` packer (`RUNTIME-087`). Exports `PointCloudVertex` (20-byte `{pos.xyz, neutral uv}` retained point layout matching `GraphVertex`), `PointCloudPackBuffer` (caller-owned scratch reused across ticks), `PointCloudPackStatus` (`Success`, `WrongDomain`, `MissingPositions`, `EmptyCloud`, `NonFinitePosition`), `PointCloudPackResult`, `PackCloud(ConstSourceView, PointCloudPackBuffer&)`, and `DebugNameForPointCloudPackStatus()`. `PackCloud` reads point positions (`v:position` on `Vertices`) into one vertex buffer in input order; the retained point pipeline draws the vertex buffer directly so no index buffer is emitted (`SurfaceIndices`/`LineIndices` stay empty). UV fields are written as neutral zeroes and are never used for normal encoding. Deleted vertex rows are packed in place (no compaction) in this slice. `LocalBounds.LocalSphere` is filled from the point AABB midpoint and half-diagonal; `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites them via `ExtractBounds`. One `GpuGeometryHandle` per cloud entity, matching the canonical single-renderable-instance contract. `Runtime.RenderExtraction` integration, `PointCloudGeometry*` diagnostics counters, eligibility-flip releases, dirty-domain reupload, and the `TickPointCloudGeometry` deferred-retire window also land in `RUNTIME-087` — see the residency prose below. |
| `Extrinsic.Runtime.PrimitiveSelectionRefinement` | Runtime-owned CPU refinement that converts a graphics `EncodedSelectionId` primitive hint into an authoritative mesh face/edge/vertex, graph edge/node, or point-cloud point selection against the entity's promoted `GeometrySources` view (`RUNTIME-093` Slice A). Exports `RefinedPrimitiveKind` (`None`/`Entity`/`Face`/`Edge`/`Vertex`/`Point`; a graph node is reported as `Vertex`), the fail-closed `PrimitiveRefineStatus` taxonomy (`Success`, `CpuFallbackResolved`, `UnsupportedDomain`, `StaleEntity`, `MissingGeometrySource`, `InvalidPrimitivePayload`, `CpuFallbackMiss`) with `DebugNameForPrimitiveRefineStatus()` and the `IsResolved()` predicate, `kInvalidPrimitiveIndex`, the `PrimitiveRefineRequest` input (echoed `EntityId`/`StableId`, the GPU `Hint`, an `EntityIsLive` liveness flag, an optional entity-local `LocalHit` anchor, the optional entity-local pick-ray fallback inputs (`HasPickRay`/`RayOrigin`/`RayDirection`/`FallbackRadius`), and the `LocalToWorld` transform), the `PrimitiveSelectionResult` (status + domain + kind + `FaceId`/`EdgeId`/`VertexId`/`PointId` (`kInvalidPrimitiveIndex` when N/A) + consistent `LocalHit`/`WorldHit` when `HasHitPosition`), and the pure, stateless `RefinePrimitiveSelection(ConstSourceView, PrimitiveRefineRequest)` entry point. The hint is treated as a hint only: every payload index is validated against authoritative CPU geometry, a `!EntityIsLive` request is rejected as `StaleEntity` before any geometry is touched, and a hint primitive domain that does not apply to the geometry domain (e.g. a `Face` hint on a graph or point cloud) fails closed as `UnsupportedDomain`. Mesh refinement maps a `Face` hint — whose 28-bit payload is the GPU per-draw triangle index (`gl_PrimitiveID`, written by `assets/shaders/selection/face_id.frag`), **not** a face row — through `Extrinsic.Runtime.MeshGeometryPacker::BuildSurfaceTriangleFaceMap` (the shared inverse of `PackMesh`'s n-gon fan-triangulation, so a quad's second triangle resolves to the quad face instead of being rejected) to the owning face row, and then, when a `LocalHit` anchor is present, walks that face's halfedge ring (`f:halfedge`/`h:next`/`h:to_vertex`, step-capped against malformed topology) to report the nearest face vertex and the nearest boundary edge (resolved to an `Edges` row via unordered endpoint match); an `Edge` hint reports the edge plus the nearest endpoint, and a `Point` hint resolves a mesh vertex (RUNTIME-088 vertex point view). Graph refinement resolves `Edge` → edge + nearest endpoint node and `Point` → node; point-cloud refinement resolves `Point` → point. The reported hit position is the request's local anchor when present, otherwise the resolved primitive's representative local position (vertex/point position, edge midpoint, face centroid), transformed by `LocalToWorld` so both local and world hit data are consistent. When the hint carries no usable sub-primitive (`SelectionPrimitiveDomain::None`, e.g. an all-zero "no hit" `EncodedSelectionId`) and the request supplies an entity-local pick ray, refinement runs the optional CPU ray fallback (`RUNTIME-093` Slice B1): it resolves the entity's nearest mesh vertex / graph node (reported as `Vertex`) / point-cloud point whose perpendicular distance to the ray (a half-line clamped at the origin, `t >= 0`) is minimal and within `FallbackRadius`, reporting `CpuFallbackResolved` with that primitive's local position transformed to world, or the deterministic `CpuFallbackMiss` when nothing qualifies, the radius is non-positive with no primitive exactly on the ray, or the ray direction is degenerate. The runtime caller owns the entity transform and transforms a world-space pick ray into local space, so the entry point stays pure and only ever maps local→world for reporting; a valid hint always wins over the ray (the fallback never overrides a resolved `Face`/`Edge`/`Point`/`Entity` hit), and a missing hint with no pick ray stays a fail-closed `UnsupportedDomain`. The module imports the promoted ECS `GeometrySources` view, the ECS `Scene::Registry`/`Transform::WorldMatrix` (for the frame-loop bridge below), and the graphics `EncodedSelectionId`/`SelectionPrimitiveDomain`/`PickReadbackResult` producer types — it never imports live ECS into graphics and mutates no selection state (the caller owns any cache mutation). The module also exports the frame-loop bridge `RefinePickReadbackResult(Scene::Registry&, const Graphics::PickReadbackResult&)` (`RUNTIME-093` Slice B2): it resolves the readback's render id (`StableEntityLookup::ToRenderId`: `entt::entity` handle + 1, `0` = background) to a live entity by decode + a `registry.valid()` version check (a recycled/destroyed slot yields a deterministic `StaleEntity` rather than refining its new occupant), reads the entity's `Transform::WorldMatrix` as `LocalToWorld` (identity when absent), builds the authoritative `ConstSourceView` from the live registry, and delegates to `RefinePrimitiveSelection`; a background (no-hit) readback resolves to `std::nullopt`. It is a pure read — it mutates neither the registry nor any selection state, so the caller owns the editor-facing cache. BUG-026 adds the context-aware overload `RefinePickReadbackResult(scene, readback, const PickReadbackContext*)` plus the exported `UnprojectPickDepth(...)` helper: `Engine` captures a per-`Sequence` `PickReadbackContext` (the issuing frame's inverse view-projection, viewport, world pick ray, and pixel-radius scale) when it drains a pick, and replays it when the matching readback arrives, so the readback's `SceneDepth` sample reconstructs the world-space cursor position against the camera that issued the pick. The reconstructed cursor is reported in both spaces on the result (`CursorFromDepth`, `WorldCursor`, `LocalCursor`, `Depth`), feeds the entity-local `LocalHit` anchor (driving the nearest-vertex/edge resolution on the hinted face/edge), and supplies the entity-local pick ray + pixel-footprint `FallbackRadius` for the missing-hint CPU fallback (perspective cameras scale the pixel radius by the hit distance; orthographic cameras — detected via the exported `IsOrthographicProjection`, e.g. the promoted `TopDownCameraController` — keep the depth-invariant `orthoHeight / viewportHeight` footprint so a top-down pick radius does not grow with camera altitude). Slice A delivers the standalone hint-based refinement core + `contract;runtime` fixture coverage (`Scaffolded`); Slice B1 adds the optional CPU ray fallback for missing hints (`CpuFallback*` statuses) + fallback `contract;runtime` coverage; Slice B2 wires the bridge into `Engine::RunFrame` (the editor-facing `m_LastRefinedPrimitive` cache exposed by `Engine::GetLastRefinedPrimitiveSelection()`, updated from each pick readback as the readback-drain loop consumes it — newest pick wins, a background readback clears it, an empty-drain frame retains the prior value) and closes `CPUContracted`. |
| `Extrinsic.Runtime.ReferenceScene` | Opt-in runtime-owned reference scene seam (GRAPHICS-029A/B, updated by RUNTIME-097). Exports `IReferenceSceneProvider`, `ReferenceSceneRegistry`, `ReferenceSceneEntity`/`ReferenceScenePopulation`, `TriangleProvider`, `MakeDefaultReferenceSceneRegistry()`, `RegisterDefaultReferenceProvidersIfAbsent()`, and `BuildReferenceCameraViewInput()`. `Engine::Initialize()` invokes `RegisterDefaultReferenceProvidersIfAbsent` so any unregistered selector receives its production default (currently `TriangleProvider` for `Triangle`), then resolves `EngineConfig::ReferenceScene::Selector` against `Engine::GetReferenceSceneRegistry()` exactly once after scene-registry construction and before `IApplication::OnInitialize`. The returned `ReferenceScenePopulation` is stored so `Engine::Shutdown()` routes teardown through the same provider before the scene registry is destroyed; the optional `CameraViewInput` seed is captured on `m_ReferenceCamera` and consumed as the initial state for `Extrinsic.Runtime.CameraControllers`. `m_ReferenceSceneInstalled` guards against double-install via `std::terminate`, and `ReferenceSceneRegistry::Resolve()` itself terminates on unregistered selectors (GRAPHICS-029 Decision 7 applied to both register and resolve). `TriangleProvider::Populate` calls `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")`, stamps a durable `StableId`, `Selection::SelectableTag`, `Graphics::Components::RenderSurface{Domain = Vertex}`, and white `Graphics::Components::VisualizationConfig`, populates mesh-domain `GeometrySources` from one finite halfedge triangle, and returns a CameraViewInput seed (position (0,0,3), forward (0,0,-1), up (0,1,0), near 0.1, far 100). |
| `Extrinsic.Runtime.RenderExtraction` | Runtime-owned ECS-to-graphics extraction cache and snapshot handoff. Extraction uses `Extrinsic.Runtime.GeometryAvailability` for `GeometrySources` lane eligibility before invoking mesh, graph, point-cloud, or primitive-view packers. `RenderExtractionCache::FindGpuRenderableAvailability(...)` exposes a read-only `GpuRenderableAvailabilityView` keyed by stable entity id, with independent surface, edge, and point lane residency plus canonical named-buffer facts; ECS remains CPU authoring state and stores no GPU handles or renderer sidecars. |
| `Extrinsic.Runtime.RenderWorldPool` | Runtime-owned multi-buffer slot-lifecycle pool for pipelined frames (`GRAPHICS-036A`, first implementation child of the retired `GRAPHICS-036` planning slice; the planning slice named it `GRAPHICS-036-Impl-A`). Exports `RenderWorldPoolDiagnostics` (the three `GRAPHICS-036` decision-7 counters: `PipelineStallCount`, `ExtractionSkipCount`, `LastConsumedFrameAge`) and the `RenderWorldPool` value type. Implements the producer/consumer slot state machine the planning slice calls "atomic swap primitives + reclamation queue": the producer (extraction) calls `AcquireBack(frameIndex)` for a free slot, writes the snapshot, and `PublishFront(slot)` (release store of a single `std::atomic` front index plus a monotonic publish-sequence bump); the consumer (renderer) calls `AcquireFront(frameIndex)` (acquire load, per-slot atomic refcount increment) and `ReleaseFront(slot)`. Buffer count defaults to 3 (triple-buffer with reclamation, decision 1), clamps to `[1, 4]`, and collapses to in-place synchronous reuse at 1. Reclamation (decision 4) returns a slot to the free list only once its refcount is zero and it is no longer the published front, drained at the start of each `AcquireBack`. Back-pressure (decision 5): producer-faster overwrites the still-unpublished back slot (`ExtractionSkipCount`); consumer-faster reuses the current front when no new publish-sequence is observed (`PipelineStallCount`), so a synchronous pool that re-publishes the same slot index every frame is never mistaken for a stall. When the producer outruns the consumer so far that every slot is a published front still held in flight (no free slot and no unpublished back), `AcquireBack` fails closed — it returns `kInvalidSlot` (still counting `ExtractionSkipCount`) so the extraction is skipped and the previous front stays current, rather than overwrite storage an in-flight frame still references. The module imports nothing from graphics/ECS/platform — it manages only slot indices and atomics, introducing no new dependency edge. `GRAPHICS-036D` extends the CPU contract to the pipelined integration path: the renderer retains per-slot snapshot storage keyed by the pool slot, and `RenderConfig::SynchronousExtraction = false` consumes `AcquirePreviousFront` to prove render-N-1 without stalls/skips while synchronous mode remains the default. `GRAPHICS-036B` surfaces the pool's three counters read-only on `RuntimeRenderExtractionStats` (`RenderWorldPipelineStallCount`, `RenderWorldExtractionSkipCount`, `RenderWorldFrameAgeFrames`) via the pure `MirrorRenderWorldPoolDiagnostics(pool, stats)` free function in `Extrinsic.Runtime.RenderExtraction`. |
| `Extrinsic.Runtime.SelectionController` | Runtime/editor-owned selection authority (`RUNTIME-089` Slice A). Exports `SelectionPickMode` (`Replace`/`Add`/`Toggle`), `SelectionPickKind` (`Hover`/`Click`), `PendingSelectionPick`, `SelectionControllerConfig`, `SelectionControllerDiagnostics`, and the `SelectionController` class. Input ports call `RequestHoverPick`/`RequestClickPick`; the controller coalesces same-frame pointer events into one `PendingSelectionPick` (a click supersedes any pending hover, latest position wins). `ConsumePendingPick` drains the survivor, assigns it a unique monotonic `Sequence`, and tracks it in a bounded FIFO of in-flight picks (multiple can be outstanding because GPU picking runs several frames in flight). `ConsumeHit(registry, stableEntityId, pickSequence)` / `ConsumeNoHit(registry, pickSequence)` resolve the exact request that produced the readback by `Sequence` and replay its kind/mode, so a hover readback only touches `HoveredTag` and a click readback only touches `SelectedTag` even when several picks are in flight or the renderer publishes results out of issue order. The correlation `Sequence` is threaded end-to-end (`PickPixelRequest` → `RenderWorld.PickRequest` → the renderer's picking slot → `PickReadbackResult`), and `SelectionSystem` holds completed readbacks in a **FIFO queue** drained by `PopPickResult` (replacing the prior single last-result holder that dropped all but the newest when `Graphics.Renderer::DrainCompletedPickingSlots` published several slots in one `BeginFrame`). No-sequence convenience overloads consume the oldest in-flight pick for callers with at most one outstanding (or uncorrelated, `Sequence == 0`, readbacks); the in-flight FIFO is bounded by `MaxTrackedInFlightPicks` (oldest evicted, `InFlightPicksDropped` bumped) so a readback that is never published cannot leak, and an unmatched sequence is applied as a configured-mode click (`UntrackedReadbacks`). Hits resolve the runtime stable entity id to a live `entt::entity` through the render-id resolution seam — when `Engine` has attached a `StableEntityLookup` (`SetStableEntityLookup`, `RUNTIME-092` Slice B) the seam routes through the sidecar's `ResolveByRenderId` (decode + live-registry validation, so a recycled/destroyed slot is rejected by the runtime-owned authority); standalone (no attached lookup) it falls back to the `ToEntityHandle` decode plus the controller's validity check. Hits reject stale (destroyed) and non-`SelectableTag` entities, and apply the documented Replace/Add/Toggle policy to the insertion-ordered selection set. The controller mirrors that set onto ECS `Selection::SelectedTag`/`HoveredTag` and maintains the `SelectedStableIds()`/`HasHovered()`/`HoveredStableId()` snapshot buffers that Slice B copies into `RenderWorld.Selection`. Default sandbox policy: single-select click, hover outline, additive/toggle via the click-mode argument, clear-on-background click (Replace mode) and clear-on-background hover. The controller module itself imports only the promoted ECS registry/handle and selection components — never graphics, platform input, or the renderer. Slice B (RUNTIME-089) wires it into the frame loop from `Engine::RunFrame`, which owns the graphics handoff: input ports/editor tools submit picks onto `Engine::GetSelectionController()`, the coalesced pick is drained (carrying its correlation `Sequence`) into `RenderFrameInput::Pick`/`HasPendingPick` and `SelectionSystem::RequestPick` before `IRenderer::ExtractRenderWorld()`, **all** completed readbacks are drained FIFO via `SelectionSystem::PopPickResult()` in the maintenance phase after present and each is resolved by its `Sequence` through the sequence-correlated `ConsumeHit`/`ConsumeNoHit` overloads (so out-of-order / multiple-in-flight readbacks each apply to the correct request), and the controller snapshot (`SelectedStableIds()`/`HoveredStableId()`/`HasHovered()`) is mirrored into `RenderWorld.Selection` via `RenderExtractionCache::ExtractAndSubmit(..., const SelectionController* selection)` → `RuntimeRenderSnapshotBatch::Selection*` → `SubmitRuntimeSnapshots` stable storage → `ExtractRenderWorld`, so graphics stays reporting-only and never reads live ECS. The selection snapshot's outline styling on `Graphics::SelectionSnapshot` keeps its recipe defaults; only the identity fields are filled here. The real input→pick binding (which mouse button/modifier maps to click vs hover and Add/Toggle) is owned by a later editor/UI task; Slice B proves the plumbing through `contract;runtime` coverage. |
| `Extrinsic.Runtime.StableEntityLookup` | Runtime-owned scene-local lookup sidecar (`RUNTIME-092`, Slice A). Exports `StableEntityLookupDiagnostics` and the `StableEntityLookup` class. Maps the optional, durable `ECS::Components::StableId` of an entity to its current live `entt::entity` (`HARDEN-068` Decision 3 left this `StableId -> entity` lookup to a runtime consumer; this is that consumer). Two resolution paths: **by `StableId`** uses a stored, maintained winner-map because `StableId` is independent of the `entt::entity` bit pattern (it survives recycling / save-load); **by render/extraction stable id** (`std::uint32_t`) decodes the id — which is `static_cast<std::uint32_t>(entt::entity) + 1`, the shifted reversible index+version encoding emitted by `RenderExtractionCache::StableEntityId` / `SelectionController::ToStableEntityId` (render id `0` is reserved for the GPU picking background sentinel, BUG-026) — and validates the handle against the registry (a recycled slot carries a bumped version, so a stale render id fails `IsValid`). `Rebuild(registry)` re-derives the winner-map from every live entity with a valid `StableId`; `Track`/`Forget` maintain it incrementally; `ResolveByStableId`/`ResolveByRenderId`/`ResolveSelected` resolve to live entities; `PruneStale` drops destroyed winners in bulk. Imports only the promoted ECS registry / handle and the `StableId` value type — never graphics or platform — and adds no lookup state to ECS. Slice A delivered the standalone sidecar (`Scaffolded`); Slice B (`RUNTIME-092`, closes `CPUContracted`) wired the rebuild into the runtime frame lifecycle (`Engine` owns the lookup, `Rebuild`s it each frame before the pick-readback drain) and routed `SelectionController`'s render-id resolution seam onto the sidecar's `ResolveByRenderId`. See the duplicate/stale policy and frame-wiring notes below. |
| `Extrinsic.Runtime.SpatialDebugAdapters` | Runtime-only translation seam from geometry-tree implementations (`Geometry.BVH`, `Geometry.KDTree`, `Geometry.Octree`, `Geometry.ConvexHull`) into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers` packet types (clarified by `GRAPHICS-011Q`; tracked by `RUNTIME-082`). Exports `SpatialDebugSnapshotBatch` (mutable per-frame output container with `Bounds`, `HierarchyNodes`, `SplitPlanes`, `ConvexHullVertices`, `ConvexHullEdges`, `PointMarkers` spans plus a `Clear()` helper), `SpatialDebugAdapterOptions` (`LeafOnly`, `OccupancyOnly`, `MaxDepth`), `SpatialDebugAdapterStats` (per-`Append` accumulator with `LeafNodeCount`, `InnerNodeCount`, `SplitPlaneCount`, `EmptyNodeSkippedCount`, `DepthCapTruncationCount`), the pure-virtual `ISpatialDebugAdapter` interface, the `BvhAdapter` (Slice A), `KdTreeAdapter`, `OctreeAdapter` (Slice B), `ConvexHullAdapter` (Slice C) concretes, and the `SpatialDebugAdapterRegistry` (Slice C) key→adapter table. Runtime is the only layer permitted to import both geometry tree implementations and the graphics packet types per `AGENTS.md` §2. `KdTreeAdapter` mirrors the BVH pattern (one `SpatialDebugSplitPlane` per inner node carrying `node.SplitAxis`/`node.SplitValue`); `OctreeAdapter` emits three perpendicular `SpatialDebugSplitPlane`s per inner node — one per axis at the parent AABB center — because `Geometry::Octree::Node` does not record the chosen split point. The center-based visualization is exact for `SplitPoint::Center` and an explicit approximation for `Mean`/`Median`, which the adapter does not attempt to reconstruct. `ConvexHullAdapter` copies the hull's V-Rep into `ConvexHullVertices` and derives `ConvexHullEdges` by plane-incidence (two vertices form a `SpatialDebugWireEdge` when they share ≥2 face planes within `IncidenceEpsilon`); it ignores `LeafOnly`/`OccupancyOnly`/`MaxDepth` and leaves `SpatialDebugAdapterStats` untouched because none of the tree-shaped concepts apply to a flat hull. `SpatialDebugAdapterRegistry` maps an opaque `std::uint64_t` renderable key onto a non-owning `const ISpatialDebugAdapter*`; callers own adapter lifetime and must `Unregister` before the adapter or its source geometry tree is destroyed. `RUNTIME-082` Slice A scaffolded the umbrella + BvhAdapter + value types; Slice B added the KdTree/Octree adapters; Slice C landed the ConvexHull adapter + registry; Slice D wires the pump through `RenderExtractionCache::ExtractAndSubmit` (`RegisterSpatialDebugAdapter` / `UnregisterSpatialDebugAdapter` transfer `std::unique_ptr<ISpatialDebugAdapter>` ownership into the cache and mirror into an embedded `SpatialDebugAdapterRegistry`; the extraction loop walks the `ECS::Components::SpatialDebugBinding` view, accumulates a shared `SpatialDebugSnapshotBatch`, attaches its spans to `RuntimeRenderSnapshotBatch::SpatialDebug{Bounds,HierarchyNodes,SplitPlanes,ConvexHullVertices,ConvexHullEdges,PointMarkers}`, and folds per-frame counters onto `RuntimeRenderExtractionStats`'s `SpatialDebug{BindingsObserved,AdaptersInvoked,MissingAdapterCount,BoundsCount,HierarchyNodeCount,SplitPlaneCount,ConvexHullVertexCount,ConvexHullEdgeCount,PointMarkerCount,LeafNodeAccumulator,InnerNodeAccumulator,EmptyNodeSkippedAccumulator,DepthCapTruncationAccumulator}` fields). The Slice D follow-up wires `Graphics::Renderer::SubmitRuntimeSnapshots` to consume those spans via the existing `BuildSpatialDebug{Hierarchy,Bounds,SplitPlane,ConvexHull,PointMarkers}Wireframes` helpers (preferring `HierarchyNodes` over `Bounds` when both are populated 1:1, to avoid double-rendering the same node bounds), routes the produced `Debug{Line,Point,Triangle}Packet` records through the same validation+clamp loop the explicit runtime debug spans use, and surfaces them via `RenderWorld::DebugPrimitives` so the canonical debug-primitive pass actually rasterizes the wireframes. |
| `Extrinsic.Runtime.SpatialDebugClosestFace` | Runtime-owned closest-face SpatialDebug overlay seam (`RUNTIME-135`). Exports a caller-resolved active mesh descriptor (`HalfedgeMesh::Mesh*`, stable mesh key, mesh revision, active flag), `SpatialDebugClosestFaceConsumer`, deterministic fail-closed status names, and a data-only `SpatialDebugClosestFaceOverlay` containing the highlighted face, probe point, closest point, normal, exact distance, primitive index, mesh key/revision, query status, and GEOM-039 diagnostics. The consumer caches a `Geometry.MeshClosestFaceIndex`, rebuilds only when the active mesh key or revision changes, and invalidates explicitly through `Invalidate()`. Runtime composes the geometry-owned query; it does not implement nearest-face traversal, import renderer/RHI/Vulkan APIs, or mutate editor/UI state. |
| `Extrinsic.Runtime.VisualizationAdapters` | Runtime-only producer seam from CPU geometry/property metadata into data-only `Extrinsic.Graphics.VisualizationPackets` records (delivered by retired `RUNTIME-083`). Slice A exports `VisualizationAdapterBatch` (mutable vectors for all visualization packet lanes plus `Clear()` and `AsPacketBatch()` span view), `VisualizationAdapterOptions` (source/output name, expected attribute domain, optional externally owned buffer device addresses, auto/manual range, colormap, overlay metadata, atlas metadata, and fragment-bake mapping), `VisualizationAdapterStats`, the pure-virtual `IVisualizationAdapter`, `PropertyScalarAdapter`, and `VisualizationAdapterRegistry`. `PropertyScalarAdapter` reads a `Geometry::ConstPropertySet` float or double property, computes a deterministic scalar range from finite CPU values (expanding flat auto-ranges to a valid non-zero interval), and emits a `Graphics::ScalarAttributePacket`; if no BDA is supplied it also emits a property-buffer upload descriptor for graphics residency. Slice B wires scalar-field extraction through `RenderExtractionCache`: the cache owns adapter instances via `RegisterVisualizationAdapter` / `UnregisterVisualizationAdapter`, stores per-renderable `VisualizationAdapterBinding` records keyed by stable entity id, invokes the bound adapter when `Graphics::Components::VisualizationConfig::Source == ScalarField`, attaches the resulting packet spans to `RuntimeRenderSnapshotBatch::Visualization*`, and reports adapter invocation/rejection plus packet-lane counters on `RuntimeRenderExtractionStats`. Mesh `GeometrySources` scalar and `glm::vec4` color visualizations can also auto-append property-buffer packets directly from the selected mesh property, keyed by stable entity id, without an explicit adapter binding. Slice C.1 adds `KMeansLabelAdapter` and `VectorFieldAdapter`: `KMeansLabelAdapter` maps finite `glm::vec4` color properties such as `v:kmeans_color` or `v:color` into `Graphics::ColorAttributePacket`, and `VectorFieldAdapter` maps finite `glm::vec3` vector properties plus caller-provided position/vector buffer BDAs, scale, color, and depth-test policy into `Graphics::VectorFieldOverlayPacket`. These C.1 adapters validate source shape and metadata but do not schedule KMeans, allocate vector-field residency, or wire non-scalar extraction selection. Slice C.2 adds `IsolineAdapter`: it reads finite float/double scalar properties, computes or validates the scalar range, validates isoline count, line width, color, and depth-test metadata, and emits `Graphics::IsolineOverlayPacket` records. The C.2 adapter does not import legacy graphics isoline extraction, allocate overlay residency, or wire non-scalar extraction selection. Slice D adds `HtexMetadataAdapter`: it emits `Graphics::HtexPatchPreviewAtlasPacket` and `Graphics::FragmentBakeAtlasPacket` records from caller-authored atlas metadata, validates texcoord requirements for UV bakes, marks accepted `ExistingTexcoords` bakes as `RuntimeResolved` with the caller texcoord dirty stamp, forwards optional generated texture `AssetId`, generated texture semantic, and source-attribute dirty stamp metadata, and schedules a deterministic `StreamingExecutor` task for valid `RecreateHtex` requests. Slice E extends `RenderExtractionCache::VisualizationAdapterBinding` with packet-kind/option metadata, selects scalar/color/isoline metadata from `VisualizationConfig` where applicable, accepts vector/Htex metadata from runtime-owned binding options, attaches non-scalar spans to `RuntimeRenderSnapshotBatch::Visualization*`, and folds non-scalar packet/error counters onto `RuntimeRenderExtractionStats`. The umbrella remains CPU-contracted: it does not allocate atlas residency, run an Htex regeneration algorithm inline, or claim backend visual proof. |
| `Extrinsic.Runtime.ImGuiAdapter` | Runtime-side Dear ImGui platform/renderer adapter (`RUNTIME-090` Slice A, the producer half declared by `GRAPHICS-013CQ`). Exports `ImGuiAdapterDiagnostics` (testable observables: `Initialized`, `FramesProduced`, `LastDrawListCount`/`LastVertexCount`/`LastIndexCount`/`LastCommandCount`, `LastFrameUsedUserTexture`, `PumpedEventCount`, `ContextRebuilds`, `EditorCallbackInvocations`, `DisplayWidth`/`DisplayHeight`) and the `ImGuiAdapter` class, constructed with `(Platform::IWindow&, Graphics::ImGuiOverlaySystem&)`. `Initialize()` creates the ImGui context, calls `ImGuiOverlaySystem::Initialize()`, and configures ImGui IO; `BeginFrame(deltaSeconds)` refreshes display metrics from the window, pumps the window's drained `Platform::Event`s into ImGui IO, and calls `ImGui::NewFrame()`; `EndFrame()` invokes the editor hook, calls `ImGui::Render()`, walks `ImDrawData` into one `ImGuiOverlayFrame` (font-atlas bytes, per-list command/vertex/index counts, copied POD vertex/index payloads, and `UsesUserTexture` derived by comparing each draw command's texture id against the font atlas), and submits it via `ImGuiOverlaySystem::SubmitFrame`; `Shutdown()` tears the overlay system + context down; `RebuildForDisplayChange()` runs exactly one `Shutdown()`+`Initialize()` cycle (DPI/font rebuild) counted in `ContextRebuilds`; `SetEditorCallback(std::function<void()>)` registers the editor hook called once per frame between begin and end; `WantsMouseCapture()` and `WantsKeyboardCapture()` expose Dear ImGui's current IO capture decisions without exporting ImGui headers. The adapter is backend-agnostic: it translates `Platform::Event` variants (cursor/button/scroll/char/resize) into ImGui IO directly rather than linking `imgui_impl_glfw`, so it drives the `Null` window as well as the GLFW backend. GLFW key-code→`ImGuiKey` translation is deferred to the editor input-binding slice (counted as pumped but not translated). ImGui is version `1.92.5`; the adapter intentionally keeps `ImGuiBackendFlags_RendererHasTextures` disabled for now because the promoted renderer consumes a copied legacy CPU font atlas through `ImGuiOverlaySystem` rather than processing `ImDrawData::Textures[]` dynamic texture requests. `imgui.h` stays out of the `.cppm` interface (only an opaque `ImGuiContext*` forward declaration is used) and `imgui_core_lib` (backend-agnostic, no glfw/volk) is linked **PRIVATE** to `ExtrinsicRuntime`, so graphics never sees ImGui types and the runtime's public surface gains no windowing/Vulkan deps. Slice A delivers the standalone adapter module + `FakeWindow`-driven `contract;runtime` coverage (`Scaffolded→CPUContracted`). Slice B wires it into `Engine` and closes `CPUContracted`: `Engine` owns the `Graphics::ImGuiOverlaySystem` instance (runtime owns composition; the allowed `runtime -> graphics` edge) and constructs the adapter in `Initialize()` after the `Window` and `Renderer` exist. `Engine::RunFrame` calls `BeginFrame(frameDt)` after `Window::PollEvents` and the minimize/resize early returns — immediately before `IApplication::OnVariableTick` — and `EndFrame()` immediately after the variable tick and before the render contract's `IRenderer::PrepareFrame()`, so exactly one `ImGuiOverlayFrame` is produced per engine frame and a minimized frame never leaves a `NewFrame()` without a matching `Render()`. After `EndFrame()`, `RunFrame` samples capture state once: mouse or keyboard capture suppresses runtime camera-controller updates and transform-gizmo input for that frame, while mouse capture suppresses viewport selection picks. The editor hook is exposed from `Engine::SetImGuiEditorCallback(std::function<void()>)` (applied to the adapter at construction so it may be registered before or after `Initialize()`), with a read-only `Engine::GetImGuiAdapter()` observer for the produce-path diagnostics; `Engine::Shutdown()` tears the adapter down first while the `Window` and overlay system it borrows are still alive. GRAPHICS-079 Slice B hands the same engine-owned overlay instance to the renderer via `IRenderer::SetImGuiOverlaySystem`, so producer and consumer share one instance. GRAPHICS-079 Slice C consumes the adapter's copied font-atlas/vertex/index payloads through the renderer-owned retained atlas and transient upload helper; GRAPHICS-079 Slice D promotes the pass to `FrameRecipe.PresentSource` and carries per-command user-texture bindless indices through the renderer pass and shader sampling. |
| `Extrinsic.Runtime.SandboxEditorUi` | Runtime-owned sandbox editor shell (`UI-001` Slices A-D, extended by `UI-002`, `UI-003`, `UI-004`, `UI-005`, `UI-006`, `UI-013`, `UI-014`, `UI-016`, `UI-017`, `UI-018`, `UI-019`, `UI-020`, `UI-021`, and `RUNTIME-098`). Exports data-only panel/domain-window models, geometry-processing capability records, deterministic missing-dependency diagnostics, primitive-detail presentation helpers, file/import and scene-file command/result DTOs, transform-edit/camera-controller/primitive-view/render-hint/spatial-debug/visualization-config/visualization-property/visualization-adapter-binding command DTO/status types, visualization-property metadata DTOs, CPU K-Means command/result DTOs, property-catalog/bound-state/UV/texture-bake DTOs, `SandboxEditorDomainWindowKind`, `SandboxEditorDomainWindowModel`, `SandboxEditorRenderGraphModel`, `BuildSandboxEditorPanelFrame(...)`, `BuildSandboxEditorDomainWindowModel(...)`, `SelectSandboxEditorEntity(...)`, `ApplySandboxEditorFileImportCommand(...)`, `ApplySandboxEditorSceneSaveCommand(...)`, `ApplySandboxEditorSceneLoadCommand(...)`, `ApplySandboxEditorTransformEdit(...)`, `ApplySandboxEditorCameraControllerCommand(...)`, `ApplySandboxEditorPrimitiveViewCommand(...)`, `ApplySandboxEditorRenderHintCommand(...)`, `ApplySandboxEditorSpatialDebugBindingCommand(...)`, `ApplySandboxEditorVisualizationConfigCommand(...)`, `ApplySandboxEditorVisualizationPropertyCommand(...)`, `ApplySandboxEditorVisualizationAdapterBindingCommand(...)`, `ApplySandboxEditorTextureBakeCommand(...)`, `ApplySandboxEditorUvRegenerationCommand(...)`, `ApplySandboxEditorKMeansCommand(...)`, `DrawSandboxEditorPanelFrame(...)`, and `SandboxEditorUi::Attach(Engine&)`/`Detach()`. The shell attaches through `Engine::SetImGuiEditorCallback`, reads scene/selection/refined-primitive state through runtime-owned APIs, emits selection changes through `SelectionController`, applies transform edits by mutating the selected entity's local `Transform::Component` and stamping `Transform::IsDirtyTag`, submits file/import path commands through `Engine::ImportAssetFromPath(...)` with explicit payload hints for mesh, point-cloud, graph, model-scene, and texture routes, observes `Engine::GetLastAssetImportEvent()` so runtime-handled dropped files update the `File / Import` panel, submits scene save/load path commands through `Engine::SaveSceneToPath(...)` / `Engine::LoadSceneFromPath(...)`, replaces camera-controller slots through the engine-owned `CameraControllerRegistry`, translates legacy mesh primitive-view commands into promoted `RenderEdges` / `RenderPoints` components, edits mesh/graph/point-cloud render hints by mutating the selected entity's promoted `RenderSurface`, `RenderEdges`, and `RenderPoints` value components, routes spatial-debug display options through the selected entity's `ECS::Components::SpatialDebugBinding`, routes material/scalar/color visualization choices through the selected entity's default `Graphics::Components::VisualizationConfig` or an optional lane override when a domain window targets a render lane, routes visualization adapter bindings through `Engine::{Set,Get,Clear}VisualizationAdapterBinding(...)` onto the engine-owned `RenderExtractionCache`, routes selected mesh texture bakes through `Extrinsic.Runtime.SelectedMeshTextureBake`, and routes selected mesh UV regeneration through `Geometry.UvAtlas` plus `GeometrySourcesPopulate` while marking runtime dirty/history state. `UI-002` adds promoted ImGui main-menu slots ordered `PointCloud`, `Graph`, and `Mesh`; each menu opens render-hint, visualization, selection-detail, and processing-discovery windows for the selected entity's current `GeometrySources` domain, with `Processing` split into submenu leaves for mesh vertices/edges/faces, graph vertices/edges/halfedges, and point-cloud vertices. The processing submenu leaves currently route to the domain processing-discovery window. `UI-003` adds the processing-discovery model: supported mesh, graph, and point-cloud source domains, stable algorithm-entry ordering, K-Means source-domain enumeration, and labels are computed from promoted `GeometrySources`. `UI-004` adds the first execution seam: CPU K-Means can run synchronously for selected mesh vertices, graph nodes, and point-cloud points, publishes `v:kmeans_label`, `v:kmeans_label_f`, `v:kmeans_color`, `p:kmeans_label`, and `p:kmeans_color` properties, stamps `DirtyVertexAttributes`, and reports fail-closed command statuses for invalid targets/inputs. `UI-005` adds promoted visualization-property enumeration and preset routing: selected mesh, graph, and point-cloud visualization windows list scalar, isoline, color-buffer, and vector-candidate properties from current `GeometrySources`, filter promoted internal/connectivity properties for actionable presets, and apply scalar, isoline, and `glm::vec4` color-buffer presets through `VisualizationConfig`. `UI-006` adds the promoted `Frame Graph` diagnostics panel: `BuildSandboxEditorPanelFrame(...)` copies renderer-owned `RenderGraphFrameStats` into a data-only model, and the attached ImGui window displays compile/execute counters, queue/timeline stats, command pass statuses, diagnostics, and the compiler debug dump without UI owning renderer state. `UI-013` adds render-window controls for mesh surface/edge/point hints, graph edge/point lanes, and point-cloud point hints through `EditorCommandHistory` when available; graph edge-lane toggles repack runtime graph residency and uniform point size/type are forwarded through extraction to retained point `GpuEntityConfig`. `RUNTIME-103` records synchronous CPU K-Means as the promoted endpoint for current workflows; broader algorithm execution, asynchronous scheduling, centroid entities, topology mutation, persistent adapter registration from borrowed property views, generic GPU residency/upload for arbitrary property arrays, and retained-line per-entity width rasterization require future value-gated runtime/editor/renderer tasks with concrete consumers. `GRAPHICS-086` retires CUDA from the promoted default path unless a future method/backend task supplies a concrete workload. `RUNTIME-098` adds the ImGui `File / Scene` window with path entry plus save/load buttons backed by the runtime-owned scene command surface. `RUNTIME-102` adds the document model in that window: it reads `EditorCommandHistory::Snapshot()` for dirty state, active path, revision counters, and undo/redo labels, and the buttons call the runtime history service when available. `UI-008` extends `File / Scene` with New, Close, Save / Save As, and Open Path controls, data-only path-entry/native-dialog boundary fields, and an app import-boundary contract that keeps `ExtrinsicSandbox` linked only to runtime. `UI-007` adds the ImGui `File / Import` payload-hint combo and runtime-observed import status for dropped files; UI does not own platform events, file decoding, `AssetService`, or ECS materialization. Mesh render windows can toggle surface, edge, and point components; graph render windows can toggle edge/point lanes and edit uniform width/point-size component values; point-cloud render windows can toggle point rendering and edit point type/size component values; visualization windows reuse the selected-entity spatial-debug, visualization-config, and visualization-property preset command surfaces, targeting surface/edge/point lane overrides from domain windows when appropriate. Domain-window models report deterministic no-selection, stale-selection, missing-scene/controller, unavailable scene-file commands, command-history, and wrong-domain diagnostics instead of caching authoritative state in UI. Adapter keys, GPU buffer addresses, `VisualizationAdapterOptions`, asset decoding, `AssetService` mutation, model-scene materialization, texture-upload requests, renderer cache state, and scene-document byte transport remain runtime/extraction/asset/core-owned inputs; UI exposes command/model data and deterministic diagnostics without owning simulation, rendering, asset state, or file IO. `imgui.h` stays confined to the `.cpp` implementation unit. Final broad file-backed sandbox visual proof remains downstream of scoped RUNTIME-095 coverage. |
| `Extrinsic.Runtime.GizmoInteraction` | Runtime/editor-owned transform-gizmo interaction (`RUNTIME-084`). Exports `GizmoMode` (`Translate`/`Rotate`/`Scale`), `GizmoAxis`, `GizmoOrientation` (`Global`/`Local`), `GizmoModifier` flags (`Snap`/`Clone`), `PickRay`, `GizmoConfig` (pick radius, axis length, translate/rotate/scale snap and scale clamps), `GizmoHitResult`, full-transform `GizmoTransformEdit` records, `GizmoUndoStack`, `GizmoInteractionDiagnostics`, `GizmoInteraction`, and `TransformGizmoRenderPacketBuilder`. `HitTest(registry, CameraViewSnapshot, cursorPixel, viewport, selected)` projects each axis handle line to screen space and resolves the nearest handle within the pixel pick radius; empty/off-handle picks are a background no-hit. `BeginDrag`/`DragTick`/`DragCommit`/`DragCancel` apply axis-constrained translate/rotate/scale edits by projecting the world `PickRay` onto the locked axis line, mutate ECS authoring transforms in runtime only, stamp `Transform::IsDirtyTag`, latch mode at drag start, honor `Snap`, and emit one before/after position-rotation-scale edit per changed entity on commit. `Engine` owns the interaction, undo stack, packet builder, selected-entity scratch, and default input binding (left mouse drag, left shift snap); extraction forwards the builder's copied `TransformGizmoRenderPacket` span through `RuntimeRenderSnapshotBatch::TransformGizmos`. Graphics sees only the frozen render packet field set and never receives drag state, pointer pixels, modifier keys, undo data, or ECS handles. |
| `Extrinsic.Runtime.StreamingExecutor` | Persistent background streaming task execution |

### Sandbox Editor Startup Layout

`UI-018` makes `Extrinsic.Runtime.SandboxEditorUi` menu-first on startup. The
first sandbox ImGui frame draws the main menu bar only; `Sandbox Editor`,
`Scene Hierarchy`, `Inspector`, `Selection Details`, `File / Scene`,
`File / Import`, `Frame Graph`, `Render Recipes`, `Camera / Render`,
`Geometry Visualization`, and all PointCloud/Graph/Mesh domain windows stay
closed until toggled from the menu. The open/closed bits live in the attached
`SandboxEditorUi` instance and do not change panel models, command routing, or
runtime ownership.

### Render Artifact Publication

`RUNTIME-127` makes renderer outputs observable as runtime artifacts before any
project data changes. `Extrinsic.Runtime.RenderArtifactPublication` stores copied
`Graphics::RenderArtifactMetadata` plus runtime-owned lifecycle kind, UI status,
payload target, provenance, diagnostics, revision counters, and audit records.
Registering an artifact is a side-effect-free declaration from the project-data
point of view. Registering a newer artifact for the same renderer/snapshot/view
recipe/output purpose with different source revisions supersedes the older
record instead of silently replacing it.

Publish and apply are explicit command surfaces. Publish requires provenance and
a target URI, and moves an unpublished artifact to `Published`. Apply requires a
published `CandidateProjectResult`, provenance, a project target, and an undo
label; it records that a project mutation is authorized for the caller-owned
command path while the registry itself performs no ECS, scene, UI, renderer, RHI,
or file-persistence mutation. Runtime/editor callers can wrap the apply command
in `EditorCommandHistory`; undo returns the artifact to `Published` and appends
an audit record. UI-facing status is deterministic across `Unpublished`,
`Stale`, `Canceled`, `Failed`, `Superseded`, `Published`, and `Applied`.

### Sandbox Editor Render Recipe Editing

`UI-023` adds the `Render Recipes` sandbox editor panel. The frame model copies
the current renderer descriptor, declared recipe slots, active view/output
recipe, binding intents, validation diagnostics, draft state, activation
revision, and render-artifact rows into data-only editor DTOs. Fixed renderer
core slots are shown as non-editable; declared extension slots and optional
binding overrides are the only rows marked editable.

Draft updates, validation, preview, activation, cancellation, artifact publish,
and artifact apply use `ApplySandboxEditorRenderRecipeCommand(...)`. Validation
and preview call the engine-owned config-control facade callback
(`Engine::PreviewRenderRecipeConfigDocument`) without mutating graphics state.
Activation calls `Engine::ApplyRenderRecipeConfigPreview(...)`, the same facade
path available to agent/CLI callers, which stores the active config on runtime
and installs a `Graphics::FrameRecipeOverride` on the renderer; the editor keeps
only widget/draft-buffer state plus a presentation cache for its panel model.
Artifact publish/apply routes through `RenderArtifactRegistry`; the registry
authorizes project mutation for accepted candidate outputs but performs no ECS,
renderer, RHI, file IO, or scene persistence mutation itself. Draft states are
explicit across inactive, debounced, validated, rejected, previewed, activated,
and canceled outcomes, so stale or invalid recipes fail closed in the UI model.

### Sandbox Editor Vertex Normals

`UI-022` adds normal-recompute editor commands at
`Mesh > Processing > Vertices > Normals`,
`Graph > Processing > Vertices > Normals`, and
`PointCloud > Processing > Vertices > Normals`. The Sandbox EditorUI surface
exports per-domain command/result pairs:
`SandboxEditorMeshVertexNormalsCommand`,
`SandboxEditorGraphVertexNormalsCommand`, and
`SandboxEditorPointCloudVertexNormalsCommand`, with matching
`ApplySandboxEditor*VertexNormalsCommand(...)` helpers. The commands validate a
live selected `GeometrySources` entity, call the domain-owned geometry modules
from `GEOM-026` (`Geometry.HalfedgeMesh.Vertices.Normals`,
`Geometry.Graph.Vertex.Normals`, or `Geometry.PointCloud.Normals`), and publish
count-matched `glm::vec3` normals to canonical `v:normal` only after the
geometry result succeeds. Successful publication stamps the precise
`DirtyVertexNormals` tag and marks editor history dirty; it does not call
renderer/RHI upload APIs or stamp broad `GpuDirty`. Mesh, graph, and
point-cloud residency extraction consume that dirty tag and perform deferred
normal-channel reupload on the next extraction opportunity. If a direct mesh
import's deferred materialization applies after an edit, runtime preserves
count-matched current `v:normal` values so editor-authored normals remain the
CPU authority.

### Sandbox Editor Mesh Denoise

`UI-024` adds a mesh-only denoise editor command at
`Mesh > Processing > Denoise`. The Sandbox EditorUI surface exports
`SandboxEditorMeshDenoiseCommand`,
`SandboxEditorMeshDenoiseResult`, and
`ApplySandboxEditorMeshDenoiseCommand(...)`. Runtime validates the selected
mesh `GeometrySources`, converts the current CPU data to a scratch halfedge
mesh, calls the geometry-owned `Geometry.Smoothing::DenoiseBilateral` kernel
from `GEOM-042`, and publishes count-matched finite positions back to canonical
`v:position` only after the geometry result succeeds. The UI exposes the
full-bilateral stage, normal/vertex iteration counts, auto-or-explicit spatial
and range sigma values, and boundary preservation, with a single `Denoise`
action. `SandboxEditorContext::MeshDenoiseKernelAvailable` provides the
deterministic unavailable-kernel diagnostic lane used by headless/editor
contract tests.

Successful publication is undoable through `EditorCommandHistory::Execute`:
undo restores the exact prior `v:position` array and redo reapplies the
denoised positions. The commit stamps `DirtyVertexPositions` and
`DirtyVertexAttributes` for deferred mesh extraction/reupload and does not call
renderer/RHI upload APIs or stamp broad `GpuDirty`. Runtime owns the ECS
composition and history seam; geometry owns the denoising algorithm.

### Sandbox Editor Point-Cloud Outlier Removal

`UI-027` adds a point-cloud-only outlier-removal editor command at
`PointCloud > Processing > Remove Outliers`. The Sandbox EditorUI surface
exports `SandboxEditorPointCloudOutlierMethod` (statistical or radius),
`SandboxEditorPointCloudOutlierRemovalCommand`,
`SandboxEditorPointCloudOutlierRemovalResult`, and
`ApplySandboxEditorPointCloudOutlierRemovalCommand(...)`. Runtime validates the
selected point-cloud `GeometrySources`, copies the point property set into a
scratch `Geometry.PointCloud.Cloud` that binds the live deletion counter and is
garbage-collected to live points first (so the operators — which iterate every
slot — see only live points and report live-relative counts, never resurrecting
dead slots), and calls the geometry-owned `GEOM-016` operators
`Geometry.PointCloud::RemoveStatisticalOutliers` /
`RemoveRadiusOutliers`. The window exposes a method toggle plus the per-method
parameters: statistical removal takes `KNeighbors` (1–512) and a
`StdDevMultiplier` (0–100, higher keeps more points); radius removal takes a
positive `SearchRadius` and a `MinNeighbors` (0–512) threshold. It surfaces the
`OutlierRemovalResult` diagnostics (kept/rejected/non-finite counts plus the
statistical mean/std-dev/threshold) and fails closed with
`InvalidProcessingParameters` / `UnsupportedGeometryDomain` / `MissingScene`
when the inputs or selection are invalid.

Unlike the vertex-normal and denoise commands, outlier removal changes the
point count, so it rebuilds the entity's point `GeometrySources` via
`GeometrySources::PopulateFromCloud`. The published cloud is the full-property
scratch cloud compacted to the kept points (the rejected slots are deleted and
garbage-collected), so every surviving per-point attribute — normals, K-Means
labels, visualization scalars — is preserved on the kept points rather than
dropped to position-only. The publication is undoable through
`EditorCommandHistory::Execute`: undo republishes the original cloud (restored
exactly, including any prior deleted slots) and redo reapplies the kept cloud.
Because the count changed,
the commit stamps coarse `GpuDirty` plus `DirtyVertexPositions` /
`DirtyVertexAttributes` / `DirtyVertexNormals` so point-cloud extraction performs
a full deferred repack/reupload on the next extraction opportunity; the command
does not call renderer/RHI upload APIs. Runtime owns the ECS composition and
history seam; `GEOM-016` owns the removal algorithm and its diagnostics.

### Sandbox Editor Progressive Poisson Sampling

`RUNTIME-134` Slices A-B add a progressive Poisson sampling playground at
`PointCloud > Processing > Progressive Poisson Sampling` and
`Mesh > Processing > Progressive Poisson Sampling`. The
Sandbox EditorUI surface exports
`SandboxEditorProgressivePoissonChannel`,
`SandboxEditorProgressivePoissonConfig`,
`SandboxEditorProgressivePoissonCommand`,
`SandboxEditorProgressivePoissonResult`, and
`ApplySandboxEditorProgressivePoissonCommand(...)`. Runtime validates selected
point-cloud `GeometrySources`, or reconstructs a selected editable mesh and
samples its triangle surface through `Geometry.PointCloud.SurfaceSampling`
before calling the METHOD-012 CPU reference backend. The command publishes
deterministic per-point float properties:

- `p:poisson_level`
- `p:poisson_phase`
- `p:poisson_splat_radius`
- `p:poisson_prefix_visible`

The UI exposes the reference sampler knobs (`dimension`, `grid_width`,
`max_levels`, `hash_load_factor`, `radius_alpha`, grid-origin randomization and
seed, shuffle toggle and seed), plus a prefix count and color-channel selector.
Mesh selections also expose surface sample count, surface seed, minimum triangle
area, and vertex-normal interpolation controls. Successful mesh runs publish the
sampled cloud back onto the selected entity via `GeometrySources::PopulateFromCloud`,
remove the stale surface render hint, enable point rendering, and report the
surface-sampling diagnostics in `SandboxEditorProgressivePoissonResult`.

The command routes `VisualizationConfig` to the selected scalar channel so
existing point colormap extraction handles the display. Prefix count `0` shows
every accepted point; positive values clamp to the accepted count and drive
`p:poisson_prefix_visible`. The published `p:poisson_phase` is a deterministic
display bucket derived from level-local rank modulo the 2D/3D phase count because
the CPU reference backend does not yet export internal phase assignments as a
stable method result.

Slice C routes the same knobs through the engine config-control facade as
`sandbox.progressive_poisson`: widget edits serialize a candidate
`EngineConfig`, preview it with `Engine::PreviewEngineConfigControlDocument`,
hot-apply it with `Engine::ApplyEngineConfigHotSubset`, and then schedule a
debounced rerun when `auto_run_on_edit` is enabled. The explicit Run button uses
the same config path before invoking the sampler command. The command only
composes runtime-owned ECS state and the public method/surface-sampling APIs; it
does not add sampler logic to UI code or call renderer/RHI upload APIs directly.
The future backend toggle remains blocked on METHOD-013.

### Sandbox Editor Mesh Curvature

`UI-026` adds a mesh-only curvature analysis editor command at
`Mesh > Processing > Curvature`. The Sandbox EditorUI surface exports
`SandboxEditorMeshCurvatureCommand`,
`SandboxEditorMeshCurvatureResult`, and
`ApplySandboxEditorMeshCurvatureCommand(...)`. Runtime validates the selected
mesh `GeometrySources`, converts the current CPU data to a scratch halfedge
mesh, calls the geometry-owned `Geometry::Curvature::ComputeCurvature` backend
from `GEOM-040`, and publishes count-matched finite vertex properties only
after the geometry result succeeds.

Successful scalar publication writes canonical `v:mean_curvature` and
`v:gaussian_curvature` `double` properties. When principal directions are
requested and available, the command also writes `v:principal_dir1` and
`v:principal_dir2` `glm::vec3` properties; when the directions lane is disabled
or unavailable, the command succeeds with scalars only and reports a
deterministic diagnostic. The UI exposes an output selector, a principal
directions toggle that is inert when directions are unavailable, and a single
`Compute` action. Successful commits are undoable through
`EditorCommandHistory::Execute`, stamp `DirtyVertexAttributes`, and do not call
renderer/RHI upload APIs or stamp broad `GpuDirty`.

`Extrinsic.Runtime.VisualizationAdapters` exports
`CurvatureVisualizationAdapter` for the published properties. It reuses the
existing scalar-property adapter path for curvature colormaps and can append
principal-direction vector-field packets from the canonical direction
properties. Missing, wrong-typed, count-mismatched, or non-finite direction
properties fall back to scalar-only output with deterministic adapter stats.

### Sandbox Editor Mesh Remesh And Subdivide

`UI-025` adds mesh topology replacement commands at
`Mesh > Processing > Remesh` and `Mesh > Processing > Subdivide`. The Sandbox
EditorUI surface exports `SandboxEditorMeshRemeshCommand`,
`SandboxEditorMeshRemeshResult`, `ApplySandboxEditorMeshRemeshCommand(...)`,
`SandboxEditorMeshSubdivideCommand`, `SandboxEditorMeshSubdivideResult`, and
`ApplySandboxEditorMeshSubdivideCommand(...)`. Runtime validates a live selected
mesh `GeometrySources` entity, builds a scratch halfedge mesh, calls the
geometry-owned `GEOM-043`/`GEOM-044` kernels, and publishes the resulting
topology back through `GeometrySourcesPopulate` only after the geometry result
succeeds.

The remesh window exposes uniform/adaptive mode, target edge length, iteration
count, project-to-surface, and mean-curvature versus error-bounded Taubin sizing
selection. Uniform mode calls `Geometry.Remeshing`; adaptive mode calls
`Geometry.HalfedgeMesh.AdaptiveRemeshing`, mapping the editor target length to a
bounded adaptive sizing range. The subdivide window exposes Loop, Catmull-Clark,
and Sqrt(3) operators, iteration count, and Loop feature-edge preservation.
Each backing kernel and option has an explicit `SandboxEditorContext` feature
gate, so unavailable operators return deterministic diagnostics without
mutating `GeometrySources`.

Successful remesh and subdivide commits are undoable through
`EditorCommandHistory::Execute`: undo restores the exact prior mesh snapshot and
redo reapplies the generated mesh. Publication stamps `DirtyVertexPositions`,
`DirtyVertexAttributes`, `DirtyEdgeTopology`, and `DirtyFaceTopology`, and does
not call renderer/RHI upload APIs or stamp broad `GpuDirty`; mesh extraction
repackages/reuploads on the next deferred extraction opportunity.

### Sandbox Editor Vertex Channel Bindings

`RUNTIME-123` extends `Extrinsic.Runtime.SandboxEditorUi` with normal/color
vertex-channel binding controls for mesh, graph, and point-cloud entities. The
property catalog exposes one target each for `VertexChannel::Normal` and
`VertexChannel::Color`, lists only the selected entity's structural vertex
domain (mesh vertices, graph nodes, or point-cloud points), and evaluates each
candidate through `VertexAttributeBinding`. Normals accept count-matched
`glm::vec3`; colors accept count-matched `glm::vec3` or `glm::vec4` and pack
through `ResolveColorChannelPackedUnorm8`. Resolver status, source/fallback
counts, and non-finite repair counts remain visible in the data-only model.

`ApplySandboxEditorVertexChannelBindingCommand(...)` mutates only the runtime
ECS descriptor `VertexChannelBindingSet`, stamps `DirtyVertexAttributes`, and
marks editor history dirty when present. It does not allocate renderer
resources, call RHI upload APIs, or persist material/asset authoring state.
Runtime render extraction reads the component and passes it to
`PackMesh`/`PackGraph`/`PackCloud`; graphics receives only the resulting
channel byte spans through public `GpuWorld` upload descriptors.

### Visualization UI Controls

`UI-019` keeps mesh, graph, and point-cloud visualization color editing in
`Extrinsic.Runtime.SandboxEditorUi`. The domain visualization windows and the
top-level `Geometry Visualization` panel route the existing uniform-color
source through `ApplySandboxEditorVisualizationConfigCommand(...)`; when
`VisualizationConfig::ColorSource::UniformColor` is active they expose an
ImGui color edit widget for the config's `glm::vec4 Color`. The UI does not
own renderer state, property-buffer residency, or graphics resource uploads.

`UI-020` adds lane-targeted visualization edits for the same command/model
surface. The top-level `Geometry Visualization` panel still edits the selected
entity's default `VisualizationConfig`; Mesh, Graph, and PointCloud domain
visualization windows target surface, edge, and point render lanes respectively
and store optional `VisualizationLaneOverrides` entries. Editability is based
on the source rows a lane needs (`Vertices`/graph nodes for points, `Edges` for
lines, faces plus vertices for mesh surfaces) rather than only on the mutually
exclusive `GeometrySources::ActiveDomain()`, so a mesh or graph can give its
rendered vertices/nodes a uniform point-lane color independently of edge or
surface color.

`UI-021` makes `Extrinsic.Runtime.GeometryAvailability` the shared availability
policy for those editor models and commands. Domain windows, visualization
targets, property catalogs, primitive-view toggles, render hints, K-Means
source choices, and mesh UV/bake diagnostics ask for the source data or render
lane they need while preserving mesh, graph, or point-cloud provenance labels.

### Progressive Editor Inspector

`UI-015` extends `Extrinsic.Runtime.SandboxEditorUi` with data-only progressive
render-data inspector models and ImGui rows. The selected-entity model reports
composition, mesh, graph, and point-cloud shapes; lane/presentation slots;
uniform default colors; compatible-first property choices with incompatible
entries disabled and explained; readiness and diagnostics; per-entity
derived-job rows from an injected `DerivedJobQueueSnapshot`; and aggregate child
summaries for composition entities. Slot default and source-property commands
route through `EditorCommandHistory` when available and mutate only
`ProgressivePresentationBindings`. UI code does not run geometry algorithms,
texture bakes, asset IO, worker jobs, or graphics uploads, and it does not
implicitly copy transient selection/highlight overlays into authored
properties.

### Geometry Property And Bake Inspector

`UI-016`, `UI-017`, and `UI-014` extend `Extrinsic.Runtime.SandboxEditorUi`
with framework24-style property and render-state inspection without importing
framework24 ownership patterns. The selected-entity property catalog enumerates
mesh vertex/edge/halfedge/face, graph node/edge, and point-cloud point
properties, including canonical topology/internal rows that visualization
presets intentionally filter out. Supported scalar, label, `glm::vec2`,
`glm::vec3`, and `glm::vec4` rows carry value kind, component count, element
count, descriptor identity, compatibility data, and selected-value previews;
unknown or unsupported property storage remains visible with deterministic
disabled reasons.

The bound-state model reports render lanes, progressive presentation slots,
source kind, property/default/texture backing, readiness, generated-output
state, retained previous output, property-catalog correlation, and derived-job
progress/diagnostics for mesh, graph, point-cloud, and composition selections.
Rows are data-only snapshots; they do not store raw property pointers, renderer
handles, GPU buffer addresses, or live `AssetService` state.

The UV/texture-bake panel reports selected-mesh `v:texcoord` availability,
count matching, finite-value diagnostics, checker-preview availability, the
promoted xatlas backend, and mesh UV regeneration command availability. The
UV regeneration command triangulates the selected mesh `GeometrySources`, runs
`Geometry.UvAtlas` with explicit user parameters, copies remapped known
vertex/face properties back to the regenerated halfedge mesh, repopulates
`GeometrySources`, stamps geometry dirty tags, and marks document dirty through
`EditorCommandHistory` when available. Texture baking consumes the property
catalog, lists mesh vertex/face bakeable sources separately from internal,
connectivity, unsupported, graph, and point-cloud rows, and calls
`Extrinsic.Runtime.SelectedMeshTextureBake`; UI code never runs the texture
baker, mutates `AssetService`, or touches graphics/RHI residency directly.

### Point And Primitive View Payloads

BUG-028 added flat-circle, surface-aligned-circle, and impostor-sphere point
rendering for mesh vertices. RUNTIME-106 moves the authoritative control surface
to the promoted render components: `RenderPoints::Type` selects flat, sphere, or
surfel rendering, and a uniform float `RenderPoints::SizeSource` is forwarded to
the retained point-sidecar `GpuEntityConfig::Point.PointSize` every frame,
including clean reuse frames. The same component vocabulary applies to mesh vertices,
graph nodes, and point-cloud points. Older primitive-view editor/engine command
surfaces are compatibility shims that translate to `RenderEdges` /
`RenderPoints`; extraction no longer consumes `MeshPrimitiveViewSettings` as a
toggle source.

Edge view sidecars prefer authored mesh `Edges` rows, but BUG-028 also derives a
unique wireframe line list from valid halfedge/face surface topology when a mesh
has no explicit edge rows. Graphics still consumes only the immutable retained
line renderable; topology traversal remains in runtime.

The shared retained point vertex format is still 20 bytes (`pos.xyz, uv`), but
the UV fields are neutral zeroes and are not used for normals. The promoted
`assets/shaders/forward/point.vert/frag` pair currently supports mode 0 flat
circles, mode 1 depth-corrected impostor spheres, and mode 2 surfel shading with
a neutral fallback normal. A future dedicated normal-buffer residency slice must
restore true normal-aligned surfel ellipses without reusing texture coordinates.

`Extrinsic.Runtime.Engine` exports `CreateReferenceEngineConfig()` so reference
applications can request the standard runtime configuration without importing
lower-layer `core` config modules directly. Applications may pass the returned
config to `Engine`; runtime remains responsible for interpreting subsystem
configuration and composition. `ResolveEngineConfigForBoot(...)` is the sandbox
boot helper layered on top of that reference value: it checks
`--engine-config`, `INTRINSIC_ENGINE_CONFIG`, and an existing
`config/engine.json` path, then uses the core-owned
`Extrinsic.Core.Config.EngineLoad` diagnostics lane to preview the file before
constructing `Engine`. Invalid or unreadable explicit files keep the reference
config and preserve diagnostics in `EngineConfigBootResult`; runtime does not
mutate a live engine from this path. `CreateReferenceEngineConfig()` flips
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

`Engine` also exposes the live agent/CLI config-control facade documented in
[`docs/architecture/runtime-config-control.md`](../../docs/architecture/runtime-config-control.md).
Recipe preview/activation uses
`PreviewRenderRecipeConfigDocument(...)`,
`LoadRenderRecipeConfigPreviewFile(...)`,
`ActivateRenderRecipeConfigDocument(...)`, and
`ApplyRenderRecipeConfigPreview(...)`. Engine-config preview uses
`PreviewEngineConfigControlDocument(...)` /
`LoadEngineConfigControlFile(...)`; hot apply is intentionally limited to
`render.default_recipe_config_path` and `sandbox.progressive_poisson` through
`ApplyEngineConfigHotSubset(...)`. All other engine-config differences are
reported as boot-only rejections and do not mutate the live engine.

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
Single-use frame-hook adapters, fixed-step/camera/input helpers, pick-context
capture, and pick-readback refinement live as private `Runtime.Engine.cpp`
helpers so `RunFrame` stays an ordered phase list while preserving the same
runtime-owned composition points.

## Derived overlay producer decision

`RUNTIME-104` classifies legacy `Graphics.OverlayEntityFactory` behavior for
current promoted workflows and deliberately adds no `DerivedOverlayProducer`
module. Cross-domain mesh, graph, and point-cloud child overlays are represented
by ordinary promoted `GeometrySources` entities when a runtime/editor command
imports or authors data; mesh edge/vertex overlays are component-driven
`RenderEdges` / `RenderPoints` lanes implemented as runtime-owned primitive-view
sidecars over the parent mesh. Current vector-field and isoline workflows use
`Extrinsic.Runtime.VisualizationAdapters` to emit data-only visualization packets
(`VectorFieldOverlayPacket` and `IsolineOverlayPacket`) into
`RuntimeRenderSnapshotBatch`; that path creates no child ECS entity, stores no
graphics/RHI handle in ECS, and leaves backend command-shape proof retired by
`GRAPHICS-085`.

The retired overlay snapshot sketches in
`docs/migration/nonlegacy-parity-matrix.md` are historical planning notes, not
current API debt. Reopen a value-gated runtime/editor task only if a concrete
current workflow cannot be represented by ordinary geometry entities,
component-driven primitive-view sidecars, transient debug packets, or existing
visualization packet spans.

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
| Render surface/edge/point hints, `VisualizationConfig`, and `VisualizationLaneOverrides` | Supported | These are CPU descriptors consumed by runtime extraction; no live GPU handles are serialized. |
| Mesh/graph/point-cloud `GeometrySources` property data | Supported | These are the current sandbox-authored geometry authorities. |
| Lights and shadow-caster tags | Deferred | Runtime extraction consumes light descriptors, but the current scene file scope does not yet define light/shadow authoring UX or compatibility policy. |
| Collider and rigid-body descriptors | Deferred | ECS authoring descriptors exist and `PhysicsBridge::Clear()` resets live sidecars, but engine-owned physics lifecycle wiring is not installed. |
| `AssetInstance::Source` and legacy scene-file asset-source reimport | Retired from scene JSON | Reimport is an explicit runtime `AssetId` operation through `AssetService` path metadata and the ingest state machine; scene files do not persist live asset-source coupling or resurrect ECS `AssetSourceRef` semantics. |
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
   Immediately after renderer initialization, runtime attempts the boot-only
   render recipe file from
   `Core::Config::RenderConfig::DefaultRecipeConfigPath` when that path is
   non-empty. Usable `RenderRecipeConfig` previews flow through the same
   `ApplyRenderRecipeConfigPreview(...)` path used by the editor and agent/CLI
   facade; missing or invalid startup files clear the active override and leave
   the derived default frame recipe in place with diagnostics recorded on
   `Engine::GetRenderRecipeState().LastApply`. Later live engine-config hot
   applies validate the referenced recipe before mutating
   `DefaultRecipeConfigPath`, so an invalid hot file preserves the current
   active recipe override.
3. CPU `FrameGraph` and `StreamingExecutor`.
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

`Engine::Run()` owns the outer loop state: if the loop exits because
`IWindow::ShouldClose()` is already true before a frame starts or becomes true
after a frame, it normalizes that path through `RequestExit()` so
`Engine::IsRunning()` is false when `Run()` returns. A configured platform
window that initializes already closed logs a deterministic runtime warning
instead of falling back to Null silently; tests that need headless frame-loop
execution should request `Core::Config::WindowBackend::Null` explicitly.

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
execution path. Runtime-owned async asset IO and visualization/Htex baking
callers submit persistent executor work directly; `Engine` no longer exposes a
frame-recorded streaming `TaskGraph` bridge. `RUNTIME-103` records synchronous
CPU K-Means as the promoted endpoint for current editor geometry-processing
workflows, so geometry algorithm callers use the executor only after a future
value-gated task identifies a concrete long-running workload.
`Extrinsic.Runtime.AssetIngestStateMachine` is the promoted ingest-state
contract for manual, dropped-file, and reimport requests. `Engine` submits
ingest records before route/decode/apply work, completes deferred geometry
imports from the executor's main-thread apply lane, and routes reimport through
same-`AssetId` `AssetService` reloads without reintroducing ECS asset-source
coupling.

`ASSETIO-005` adds the runtime-owned AssetIO queue snapshot on top of that
state machine. `Runtime.AssetIngestStateMachine` exports queue DTOs with stable
operation handles, source paths, payload kinds, asset ids when known,
enqueue/start/finish timestamps, coarse stages (`Queued`, route/decode,
main-thread apply, GPU upload, terminal states), determinate vs indeterminate
progress, and terminal diagnostics. `Engine::GetAssetImportQueueSnapshot()`
polls those records for editor/UI consumers, marks only active deferred
`StreamingExecutor` geometry imports as cancellable, and exposes
`CancelAssetImport(...)` / `ClearCompletedAssetImports()` without moving asset,
ECS, graphics, or UI ownership below runtime. Manual imports and reimports still
use the same state-machine records, but they usually reach a terminal queue row
inside the synchronous command call.

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

For direct mesh imports that stay on the runtime-authored `GeometrySources`
residency lane, extraction also accepts data-only
`Graphics::MaterialTextureAssetBindings` keyed by stable render id. The engine
uses that seam for generated normal textures baked from CPU `v:normal` until
`GRAPHICS-104` replaces the bake with an asynchronous GPU object-space normal
job. Generated normal bindings are tagged
`Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal`; authored normal
texture bindings keep the tangent-space default and do not set the object-space
shader flag. The cache resolves the `AssetId` bindings against
`GpuAssetCache` onto the sidecar-owned `StandardPBR` material lease during
extraction. This keeps ECS free of graphics handles while letting direct
OBJ-style mesh imports shade from a generated normal map after queued runtime
materialization has resolved finite UVs. Valid authored UVs are preserved by
default in that downstream task; missing or invalid source UVs run through the
`Geometry.UvAtlas` xatlas backend before the bake, and backend failure stays
fail-closed for the deferred post-process result while the already-published raw
geometry remains live.

Progressive presentation bindings are consumed during the same extraction pass
as data-only descriptors. `BuildProgressivePresentationSnapshot(...)` resolves
slot state against the current `GeometrySources` view and folds counters onto
`RuntimeRenderExtractionStats`: entity/slot counts, default slots, pending slots,
ready texture slots, ready property buffers, unsupported slots, diagnostics, and
previous-output retention. Ready authored/generated mesh surface texture slots
are exposed as material texture binding requests keyed by stable render id;
pending, failed, unsupported, or previous-output-retained slots report
diagnostics without blocking geometry residency. Graph and point-cloud
properties remain direct property-buffer candidates in the snapshot model, with
separate graph vertex/edge domains and point-cloud color/scalar/size/normal
domains; unsupported mesh surface direct property buffers fail closed until a
future renderer-owned data path exists.

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
and clears the slot's source-asset sentinel. The mesh packer also resolves a
count-matched vertex `v:color` property into the optional
`GeometryUploadDesc::PackedVertexColors` stream using
`ResolveColorChannelPackedUnorm8`; the active deferred GpuScene path consumes
that stream through `GpuGeometryRecord::ColorBufferBDA` in the default-recipe
GpuScene surface/GBuffer shader pair, while missing or unmatched colors leave
material shading unchanged. Subsequent clean extractions for the same entity
short-circuit through the `MeshGeometry` handle and increment
`MeshGeometryReuseHits` without re-packing. Subsequent dirty extractions
(`DirtyVertexPositions` / `DirtyVertexAttributes` / `DirtyFaceTopology` /
`DirtyEdgeTopology` / `GpuDirty` any-of tag set on the entity by an ECS
producer) repack the mesh, upload a fresh
`GpuGeometryHandle`, swap the instance binding via `SetInstanceGeometry`,
enqueue the prior handle into the same `framesInFlight` deferred-retire window
the procedural cache uses, drain the dirty tags from the entity, and increment
`MeshGeometryReuploads` + `MeshGeometryReleases` (RUNTIME-085 Slice C). The bridge
is fail-closed: `MeshPackStatus::MissingPositions`, `InvalidTopology`,
`MissingTexcoords`, and `NonFiniteTexcoord` each have their own counters; every
other non-`Success` status (`MissingHalfedgeTopology`, `MissingFaceTopology`,
`EmptyMesh`, `NonFinitePosition`, `DegenerateAllFaces`, `WrongDomain`) folds
into `MeshGeometryFailedPack`. A failed pack does not bind stale geometry,
leaves the slot's source-asset sentinel cleared, and does not allocate a
`GpuGeometryHandle`.

#### Vertex attribute binding (`RUNTIME-120`)

`Extrinsic.Runtime.VertexAttributeBinding` is the reusable, GPU-agnostic helper
that maps a named geometry property to a logical vertex channel (position /
normal / texcoord / color / tangent / custom) with fail-closed diagnostics. A
`VertexAttributeBinding` names the source property, its element type, a fallback
value, and per-channel policy (renormalize vec3, allow fallback); the
`ResolveVec3Channel` / `ResolveVec2Channel` / `ResolveColorChannelPackedUnorm8`
entry points return an `AttributeBindResult` carrying a precise
`AttributeBindStatus` (`Bound` / `EmptyBinding` / `PropertyMissing` /
`TypeMismatch` / `CountMismatch`) plus source/fallback/non-finite counters. The
mesh packer resolves its normal, texcoord, and default `v:color` channels
through this helper, so the normalize-or-`+Z`, finite-or-zero, and packed-unorm8
color behavior is centralized rather than inlined per packer. This is the
structural vertex stream and is distinct from the graphics-layer
`VisualizationConfig` sci-vis colormap overlays.

`Extrinsic.Runtime.VertexChannelBindings` adds the data-only
`VertexChannelBindingSet` ECS component (`RUNTIME-123`). It persists optional
normal/color source overrides as property names plus `AttributeSourceType` and
is consumed only by runtime packers/extraction. With no binding component the
mesh packer keeps the canonical defaults (`v:normal` with +Z fallback and
optional `v:color` autodetect). With an enabled binding, mesh, graph, and
point-cloud packers resolve the selected vertex-domain property through the same
resolver and publish the resulting `NormalBytes` / `PackedVertexColors` spans
into `GpuWorld::GeometryUploadDesc`. Invalid optional bindings fail closed for
that channel without calling graphics/RHI code from the editor path.

`Extrinsic.Runtime.VertexChannelStreams` is the CPU Structure-of-Arrays
substrate for that work (ADR-0022): a `VertexLayout` (ordered channels, offsets,
stride) plus per-channel SoA byte buffers. Mesh, graph, point-cloud, and mesh
primitive-view packers
still fill their compatibility `PackedVertexBytes` scratch, but their
`GeometryUploadDesc` also points at position/texcoord/normal/color channel
spans. `GpuWorld` uploads those spans into per-channel sub-ranges of the managed
vertex buffer, and active GpuScene shaders fetch from the per-channel BDAs in
`GpuGeometryRecord`. Per ADR-0022 the engine commits to uniform SoA storage with
per-channel dirty streaming (one vertex layout, one shader fetch path); the AoS
fast lane for static geometry is deferred to the profile-gated `RUNTIME-125`.
`RUNTIME-124` maps `DirtyVertexPositions`, `DirtyVertexTexcoords`,
`DirtyVertexNormals`, and `DirtyVertexColors` to in-place
`GpuWorld::UpdateGeometryChannels(...)` writes, with legacy
`DirtyVertexAttributes` treated as a broad texcoord/normal/color channel signal.
Topology, lane-mask, coarse `GpuDirty`, and vertex-count changes fall back to a
full `GpuWorld::UploadGeometry(...)` replacement.
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
`MeshGeometryInvalidTopology`, `MeshGeometryMissingTexcoords`,
`MeshGeometryNonFiniteTexcoords`, `MeshGeometryReleases`, and
`MeshGeometryFreeRetires` (RUNTIME-085 Slices B + C). `Uploads` counts first-time
per-entity uploads, `ReuseHits` counts clean-frame rebinds, `Reuploads` counts
dirty-frame repack-and-replace events (a Reupload also increments `Releases`
because the prior handle is queued for retire), and `FreeRetires` is the per-
tick delta of actual `GpuWorld::FreeGeometry` calls fired by `TickMeshGeometry`
once the `framesInFlight` window elapses.

When `BuildConstView` resolves `Domain::Graph` instead (a graph entity carrying
`RenderEdges` and/or `RenderPoints`), `ExtractAndSubmit()` routes the view
through `Extrinsic.Runtime.GraphGeometryPacker::PackGraph(view, wantLines,
wantPoints, GraphPackBuffer&)` against a runtime-owned `GraphPackBuffer` scratch,
where `wantLines`/`wantPoints` are derived from the `RenderEdges`/`RenderPoints`
hints (RUNTIME-086 Slice B). Node positions form one shared vertex buffer; the
point lane draws it directly and the line lane indexes it via validated
`(e:v0, e:v1)` pairs, so a graph entity owns exactly one `GpuGeometryHandle`
recorded in a sidecar-owned `GraphGeometry` field (distinct from `MeshGeometry`;
mesh and graph domains are mutually exclusive per entity). When both lanes are
enabled, extraction binds the line and point lanes to separate retained
instances that share the graph geometry handle, which lets the edge and point
lanes consume independent effective visualization configs from
`VisualizationLaneOverrides` while preserving one geometry upload. Clean
re-extractions hit `GraphGeometryReuseHits`; dirty re-extractions
(`DirtyVertexPositions` /
`DirtyVertexAttributes` / `DirtyEdgeTopology` / `GpuDirty` any-of tag set on the
entity) repack, upload a fresh handle, enqueue the prior handle into a
graph-residency deferred-retire queue, drain the dirty tags, and increment
`GraphGeometryReuploads` + `GraphGeometryReleases` (RUNTIME-086 Slice C). The
sidecar also records the render-lane mask (`RenderEdges` / `RenderPoints`) the
resident upload was packed for; a change in requested lanes — e.g. a points-only
graph that later gains `RenderEdges` — repacks through the same reupload path
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
renderable through the `RenderPoints` hint — `RenderSurface`/`RenderEdges` have
no faces/edges to draw from a cloud — so a point-cloud-domain entity without
`RenderPoints` is not bound (and any prior point-cloud residency is released by
the eligibility-flip path), which is why a mesh that loses its topology back to
a bare vertex set is not silently re-bound as points. Point positions form the
vertex buffer (no index buffer), so a cloud entity owns exactly one
`GpuGeometryHandle` recorded in a sidecar-owned `PointCloudGeometry` field
(distinct from `MeshGeometry`/`GraphGeometry`; the three domains are mutually
exclusive per entity). Only a uniform screen-space point size (the `float`
alternative of `RenderPoints::SizeSource`) is supported in this slice; a
per-point size buffer (the `std::string` alternative) requires a per-point
size upload that is not implemented here and fails closed into
`PointCloudGeometryFailedPack` rather than uploading mis-sized geometry. The
runtime extraction record also forwards the retained `RenderPoints` component
to graphics synchronization so a uniform point size and render type are copied into
`GpuEntityConfig::Point.PointSize` / `Point.PointMode` for retained point passes. Clean
re-extractions hit `PointCloudGeometryReuseHits`; dirty re-extractions
(`DirtyVertexPositions` / `DirtyVertexAttributes` / `GpuDirty` any-of tag set on
the entity — a cloud has no edge/face topology) repack, upload a fresh handle,
enqueue the prior handle into a point-cloud-residency deferred-retire queue,
drain the dirty tags, and increment `PointCloudGeometryReuploads` +
`PointCloudGeometryReleases`. The bridge is fail-closed:
`PointCloudPackStatus::MissingPositions`/`EmptyCloud` fold into
`PointCloudGeometryMissingPositions`, `NonFinitePosition` into
`PointCloudGeometryInvalidPoints`, and `WrongDomain`, unsupported
`RenderSurface`/`RenderEdges` requests, plus the unsupported size-source variant
fold into `PointCloudGeometryFailedPack`; a first-attempt failed pack binds no
stale geometry, and a dirty-reupload failure — or a resident cloud switching to
an unsupported per-point size source or unsupported non-point lane — releases the
prior residency (queued for the deferred-retire window, instance detached,
`PointCloudGeometryReleases` incremented) so invalid point data does not keep
rendering, while the dirty tags stay set for later recovery. Eligibility flips
(the entity gains a procedural/asset source, loses point-cloud-domain topology,
drops `RenderPoints`, or flips to mesh/graph domain), `RetireMissingRenderables`,
and `Shutdown` route the point-cloud upload through the same deferred-retire window, incrementing
`PointCloudGeometryReleases`; the per-tick free-count delta surfaces as
`PointCloudGeometryFreeRetires` on the next `ExtractAndSubmit`. The retire queue
is driven from the maintenance phase by
`RenderExtractionCache::TickPointCloudGeometry`, mirroring `TickGraphGeometry`.
The point-cloud counter fields on `RuntimeRenderExtractionStats` are
`PointCloudGeometryUploads`, `PointCloudGeometryReuseHits`,
`PointCloudGeometryReuploads`, `PointCloudGeometryFailedPack`,
`PointCloudGeometryMissingPositions`, `PointCloudGeometryInvalidPoints`,
`PointCloudGeometryReleases`, and `PointCloudGeometryFreeRetires` (RUNTIME-087).

On top of the mesh-domain residency bridge, a mesh entity composes render lanes
by component presence (RUNTIME-106): `RenderSurface` requests filled triangles,
`RenderEdges` requests mesh wireframe/edge lines, and `RenderPoints` requests
vertex points. Surface rendering still uses `BindMeshGeometry` and owns the
surface `MeshGeometry` handle. Edge and vertex rendering use the RUNTIME-088
runtime-sidecar implementation, but the desired state and point configuration now
come from ECS components rather than `MeshPrimitiveViewSettings`.

Each requested edge/vertex lane is derived from the *same* authoritative mesh
`GeometrySources` via `Extrinsic.Runtime.MeshPrimitiveViewPacker::PackMeshEdgeView`
or `PackMeshVertexView` against one shared cache-owned `MeshPrimitiveViewBuffer`
scratch, uploaded to its **own** `GpuWorld` instance + `GpuGeometryHandle`
recorded in the parent sidecar (`MeshEdgeViewInstance`/`MeshEdgeViewGeometry`,
`MeshVertexViewInstance`/`MeshVertexViewGeometry`), and re-submitted to
`m_Transforms` every frame as an extra `GpuRender_Line | GpuRender_Unlit` or
`GpuRender_Point | GpuRender_Unlit` lane carrying the parent entity transform,
bounds, and material slot. Surface residency is not a prerequisite: a mesh with
only `RenderEdges`, only `RenderPoints`, or all three render components produces
the requested independent retained lanes over one mesh data source, with no ECS
storage of graphics handles and no mesh-topology traversal pushed into
`src/graphics/*`. The edge and vertex lanes consume their effective
visualization config from `VisualizationLaneOverrides::Edges` /
`VisualizationLaneOverrides::Points` when present, otherwise falling back to
the entity default `VisualizationConfig`; uniform colors are copied into their
retained `GpuEntityConfig::UniformColor` records independently of the surface
lane. The vertex lane writes `GpuEntityConfig::Point.PointSize` from a
uniform screen-space pixel `RenderPoints::SizeSource` and `Point.PointMode` from
`RenderPoints::Type` on every submitted frame so flat, surfel, and
impostor-sphere changes apply without forcing a geometry reupload.

The edge/vertex lanes repack on the same coalesced mesh dirty signal the surface
uses (`GpuDirty` / `DirtyVertexPositions` / `DirtyVertexAttributes` /
`DirtyFaceTopology` / `DirtyEdgeTopology`, snapshotted before any surface upload
drains the tags), so a
vertex-position edit updates both views' geometry and an edge-topology edit
updates the edge view's line indices. A repack enqueues the prior view handle
into a shared mesh-primitive-view deferred-retire queue and increments
`Mesh{Edge,Vertex}ViewReuploads` + `Mesh{Edge,Vertex}ViewReleases`. The bridge is
fail-closed per lane: the edge view folds `MissingPositions`/`EmptyMesh` into
`MeshEdgeViewMissingPositions`, reports `MissingEdgeTopology` only when neither
explicit edges nor derivable surface topology are available, reports out-of-range
explicit endpoints or malformed derived rings in `MeshEdgeViewInvalidEdges`, and
folds `WrongDomain`/`NonFinitePosition` into `MeshEdgeViewFailedPack`; the vertex
view uses `MeshVertexViewMissingPositions` and `MeshVertexViewFailedPack`,
including the unsupported named/per-point size-source case. A failed view pack
drops just that view (its instance freed, its geometry queued for retire) without
disturbing the surface mesh or the other view, and reappears on a later dirty
frame once the source recovers. Removing the matching render component, flipping
away from mesh-domain data, procedural/asset take-over, `RetireMissingRenderables`,
and `Shutdown` release the view sidecars. Edge and vertex view handles share one
retire queue driven from the maintenance phase by
`RenderExtractionCache::TickMeshPrimitiveViewGeometry`, mirroring
`TickMeshGeometry`; the per-tick free-count delta surfaces as the shared
`MeshPrimitiveViewFreeRetires`. The mesh-primitive-view counter fields on
`RuntimeRenderExtractionStats` are `MeshEdgeViewUploads`,
`MeshEdgeViewReuseHits`, `MeshEdgeViewReuploads`, `MeshEdgeViewReleases`,
`MeshEdgeViewFailedPack`, `MeshEdgeViewMissingPositions`,
`MeshEdgeViewMissingEdgeTopology`, `MeshEdgeViewInvalidEdges`,
`MeshVertexViewUploads`, `MeshVertexViewReuseHits`, `MeshVertexViewReuploads`,
`MeshVertexViewReleases`, `MeshVertexViewFailedPack`,
`MeshVertexViewMissingPositions`, and `MeshPrimitiveViewFreeRetires`. BUG-028 and
RUNTIME-106 add CPU/null regression proof for UI command routing, component-driven
sidecar extraction, point config propagation, and GLSL mode selection; broader
file-backed GPU screenshot proof remains owned by the working-sandbox acceptance
lane.

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

# Runtime Backlog Index

This README is the agent-facing entry point into the `tasks/backlog/runtime/`
queue. It lists runtime-owned backlog work and cross-links rendering tasks
whose ownership lives in `src/runtime` even when the task file is filed under
another backlog directory.

## Runtime backlog tasks

### bcg geometry-processing port integration (seeded 2026-06-26)

Core/runtime work paired with the `bcg_code_base` geometry port gaps tracked in
[`tasks/backlog/geometry/README.md`](../geometry/README.md). The core container
lives here because `geometry` may not own a `core` container; the SpatialDebug
consumer lives here because `runtime` owns composition over `geometry`.

`CORE-004` is retired; `Extrinsic.Core.IndexedHeap` now backs
`Geometry.Graph.ShortestPath` Dijkstra with a true decrease-key frontier.
`RUNTIME-135` is retired; the SpatialDebug closest-face consumer now lives in
`Extrinsic.Runtime.SpatialDebugClosestFace`. Editor method windows for the
ported algorithms are retired `UI-024`/`UI-025`/`UI-026` under the UI backlog.

### CPU→GPU vertex-attribute overhaul (Theme B)

A reusable, flexible, fast CPU→GPU vertex-attribute pipeline. Today each
geometry kind has its own packer with inlined property names and a fixed AoS
vertex struct, no vertex color channel, and no way to bind an arbitrary property
as normals/colors. This series fixes that incrementally.

`RUNTIME-125` is retired at `CPUContracted`: it added the PR-fast
SoA-vs-interleaved probe benchmark and planning-only storage/promotion
contracts without adopting an AoS lane. `RUNTIME-139` owns the optional
operational AoS storage path, shader variants, promote-on-edit behavior, and
`gpu;vulkan` parity evidence.

Storage model is fixed by
[`ADR-0022`](../../../docs/adr/0022-vertex-storage-soa-per-channel-streaming.md):
uniform SoA with per-channel streaming.

### CPU↔GPU transfer foundation — readback leg (Theme B)

The forward (CPU→GPU) transfer/binding/scheduling spine and runtime readback leg
are retired: RUNTIME-120's vertex-attribute resolver, RUNTIME-121's structural
mesh vertex-color upload path, RUNTIME-112's `DerivedJobRegistry`,
GRAPHICS-084's visualization property buffers, RUNTIME-124's per-channel
partial uploads, GRAPHICS-095/096/097/098's GPU transfer foundation, and
RUNTIME-126's readback job/write-back path now compose the current promoted
foundation. RUNTIME-126 adds a readback job kind (driving GRAPHICS-096
`DownloadBuffer`) and a readback-to-property write-back binding
(dimension-checked via GRAPHICS-095), so algorithms can chain follow-ups on
GPU-computed results ("compute -> read back -> derive color/vector-field ->
re-upload -> visible") using the existing `SubmitFollowUp`/`DependsOn` edges.
This foundation is recorded in
[`ADR-0023`](../../../docs/adr/0023-cpu-gpu-transfer-foundation.md).

`RUNTIME-111` through `RUNTIME-115` are retired; additional progressive
render-data follow-ups should open as value-gated tasks with a concrete
consumer.

### Render output artifact publication (Theme B)

Renderer outputs become runtime-owned artifacts before any project data changes.
This keeps output lifetime, diagnostics, provenance, and publish/apply behavior
observable to UI, agents, tests, and reproducibility tooling.

`RUNTIME-127` is retired; runtime now has a render artifact registry, lifecycle
states, UI-facing status vocabulary, and explicit provenance-carrying
publish/apply commands for candidate renderer outputs.

### Runtime adapter umbrellas (clarified by Q tasks; producer modules)

These open the runtime-side producer/owner umbrellas already clarified by the
done-task `Q` follow-ups. Each unblocks one or more rendering pass families.


### Working sandbox runtime path

These tasks fill the runtime-owned gaps between the renderer pass DAG and a
usable sandbox that can show authored mesh, graph, and point-cloud data with
selection and UI. They are ordered after the visible-triangle foundation and
compose with the rendering tasks listed in `tasks/backlog/rendering/README.md`.

`RUNTIME-134` is retired at `CPUContracted`; the Sandbox now exposes the
progressive-Poisson playground for selected point-cloud and mesh inputs.
`RUNTIME-136` is retired at `CPUContracted`; the Sandbox now exposes CPU/GPU
backend selectors for K-Means and Progressive Poisson, with requested-vs-actual
backend readouts and fallback diagnostics. See
[`RUNTIME-136`](../../done/RUNTIME-136-sandbox-method-backend-selectors.md).

`RUNTIME-138` is the runtime-owned selected-entity responsiveness task. It
makes the Sandbox editor path read cached selected-entity state, submit
commands/jobs, and move heavy property/channel/UV/scalar derivations out of the
ImGui callback into generation-keyed async runtime jobs. The first landed slice
is visibility-gated model construction plus per-frame domain-window model reuse;
the async cache/job and bounded-apply slices remain open.


## Cross-linked rendering tasks (runtime-owned)

Some rendering backlog tasks are runtime-owned for extraction/wiring even
though they may be filed under another task queue. Runtime reviewers must treat
these as runtime work when scheduling and review:

- `RUNTIME-127` is retired. `GRAPHICS-084` retired the runtime-adapter/property-
  selection side of visualization property-buffer residency; `GRAPHICS-084C`
  retired the opt-in Vulkan smoke evidence.

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`tasks/backlog/rendering/README.md`](../rendering/README.md) — rendering
  backlog DAG and selection rules.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [RUNTIME-125 — Optional AoS fast lane for static geometry](../../done/RUNTIME-125-aos-static-fast-lane.md)
  (done, 2026-07-02, `CPUContracted`): PR-fast SoA/probe benchmark evidence and
  planning-only storage-lane/promotion contracts landed without allocating an
  AoS GPU lane or selecting shader variants. Operational AoS storage/shaders,
  promote-on-edit behavior, and Vulkan parity remain owned by open follow-up
  `RUNTIME-139`.
- [RUNTIME-135 — SpatialDebug closest-face picking via accelerated mesh query](../../done/RUNTIME-135-spatialdebug-closest-face-picking.md)
  (done, 2026-06-28, `CPUContracted`): runtime now has a data-only closest-face
  SpatialDebug overlay consumer that caches the GEOM-039 mesh closest-face
  index by active mesh key/revision and emits deterministic fail-closed overlay
  diagnostics without renderer/RHI/Vulkan or editor/UI ownership.
- [CORE-004 — Indexed decrease-key min-heap container and Dijkstra adoption](../../done/CORE-004-indexed-decrease-key-heap.md)
  (done, 2026-06-28, `CPUContracted`): core now exports
  `Extrinsic.Core.IndexedHeap`, a deterministic fail-closed indexed binary
  min-heap with `TryTop`, `TryPop`, O(log n) `DecreaseKey`, and O(log n)
  `Remove`; geometry Dijkstra now uses it as the live frontier while preserving
  shortest-path and diagnostic parity against the prior lazy priority-queue
  implementation.
- [RUNTIME-132 — Lift single-use RunFrame hook adapters out of the RunFrame body](../../done/RUNTIME-132-lift-runframe-hook-adapters.md)
  (done, 2026-06-28): `Engine::RunFrame` now delegates single-use frame-hook
  adapters, fixed-step substeps, camera/gizmo/selection input, BUG-026
  pick-context capture, and completed pick-readback refinement to private
  implementation helpers while preserving the documented frame order.
- [RUNTIME-128 — Default lit material for material-less imported primitives](../../done/RUNTIME-128-default-lit-material-for-materialless-imports.md)
  (done, 2026-06-28, `CPUContracted`): model-scene imports now give
  material-less primitives a neutral lit StandardPBR default while preserving
  slot 0 as the unlit missing/invalid material indicator.
- [RUNTIME-127 — Render artifact publication and apply semantics](../../done/RUNTIME-127-render-artifact-publication.md)
  (done, 2026-06-24, `CPUContracted`): runtime now has a render artifact
  registry, lifecycle states, UI-facing status vocabulary, and explicit
  provenance-carrying publish/apply commands for candidate renderer outputs.
- [RUNTIME-109 — Extensible mesh attribute texture bake pipeline](../../done/RUNTIME-109-extensible-mesh-attribute-texture-bakes.md)
  (done, 2026-06-15, `CPUContracted`): generic runtime CPU mesh-attribute
  texture bakes now cover resolved-UV vertex/face scalar, label, vector2,
  vector3/normal, and RGBA outputs with stable generated texture keys.
- [RUNTIME-120 — Reusable vertex attribute binding resolver](../../done/RUNTIME-120-vertex-attribute-binding-resolver.md)
  (done, 2026-06-24, `CPUContracted`): runtime now has a CPU-only
  property-to-vertex-channel resolver with fail-closed diagnostics, and the mesh
  packer routes normal and texcoord reads through it without behavior change.
- [RUNTIME-121 — Per-vertex color channel through the geometry vertex stream](../../done/RUNTIME-121-vertex-color-channel-upload.md)
  (done, 2026-06-24, `Operational`): mesh `GeometrySources` now resolve
  count-matched `v:color` into packed unorm8 upload data, publish it through
  `GpuGeometryRecord::ColorBufferBDA`, consume it in the active default-recipe
  GpuScene surface/GBuffer shader path, and prove the path with CPU contracts,
  dirty-reupload coverage, and an opt-in `gpu;vulkan` smoke.
- [RUNTIME-122 — GPU SoA vertex channel storage and shader fetch](../../done/RUNTIME-122-gpu-soa-vertex-channel-storage-and-shader-fetch.md)
  (done, 2026-06-24, `Operational`): runtime mesh, graph, point-cloud, and mesh
  primitive-view packers now emit explicit channel streams; graphics stores
  position, texcoord, normal, and color data as managed SoA channel ranges,
  publishes per-channel BDAs through `GpuGeometryRecord`, and the active
  GpuScene surface, depth, selection, line, and point shaders fetch through the
  channel BDAs. Focused CPU coverage, the default CPU gate, structural
  validators, and opt-in `gpu;vulkan` surface plus line/point smokes passed.
- [RUNTIME-123 — Editor "bind any property as normals / colors"](../../done/RUNTIME-123-editor-bind-property-as-channel.md)
  (done, 2026-06-24, `CPUContracted`): runtime now has a
  `VertexChannelBindingSet` ECS descriptor consumed by mesh, graph, and
  point-cloud packers; the Sandbox Editor property catalog exposes normal/color
  binding targets, validates candidate properties through the
  `VertexAttributeBinding` resolver, persists per-entity bindings, and stamps
  `DirtyVertexAttributes` without direct renderer/RHI upload calls.
- [RUNTIME-124 — Per-channel dirty tracking and partial GPU uploads](../../done/RUNTIME-124-per-channel-partial-uploads.md)
  (done, 2026-06-24, `Operational`): ECS exposes fine-grained vertex-channel
  dirty tags for positions, texcoords, normals, and colors; runtime extraction
  maps resident mesh, graph, and point-cloud edits to `GpuWorld` channel update
  masks; graphics writes only changed SoA channel ranges and preserves full
  uploads for topology, vertex-count, and storage-layout changes.
- [RUNTIME-126 — GPU readback jobs and result→property write-back in the derived-job graph](../../done/RUNTIME-126-gpu-readback-jobs-and-property-writeback.md)
  (done, 2026-06-25, `Operational`): runtime readback jobs now park in
  `WaitingForReadback`, resume through `DerivedJobRegistry::DrainReadbacks()`
  after transfer delivery, write dimension-checked bytes into typed geometry
  properties, expose readback diagnostics, and keep follow-up jobs pending until
  write-back apply completes.
- [RUNTIME-119 — GPU renderable availability snapshot](../../done/RUNTIME-119-gpu-renderable-availability-snapshot.md)
  (done, 2026-06-19, `CPUContracted`): `RenderExtractionCache` now exposes a
  read-only GPU availability view keyed by stable entity id, with independent
  surface, edge, and point lane residency plus named-buffer facts.
- [RUNTIME-118 — Geometry availability consumer migration](../../done/RUNTIME-118-geometry-availability-consumer-migration.md)
  (done, 2026-06-19, `CPUContracted`): runtime packers, extraction,
  progressive property resolution, selected bake validation, and primitive
  refinement now consume source/provenance availability instead of using exact
  `ActiveDomain` as the common capability gate.
- [RUNTIME-117 — Geometry availability and render-lane resolver](../../done/RUNTIME-117-geometry-availability-render-lane-resolver.md)
  (done, 2026-06-19, `CPUContracted`): runtime now owns the standard resolver
  over ECS source availability plus `RenderSurface`, `RenderEdges`, and
  `RenderPoints`.
- [RUNTIME-115 — Selected mesh bake command surface](../../done/RUNTIME-115-selected-mesh-bake-command-surface.md)
  (done, 2026-06-17, `CPUContracted`): selected mesh property texture bakes
  now route through a runtime-owned command surface with validation, generated
  texture payload reload, optional progressive binding updates, command-history
  dirtying, synchronous test hooks, and observable derived-job apply/stale
  diagnostics.
- [RUNTIME-114 — Progressive import enrichment pipeline](../../done/RUNTIME-114-progressive-import-enrichment-pipeline.md)
  (done, 2026-06-16, `CPUContracted`): model-scene mesh leaves can publish raw
  geometry immediately, attach progressive surface bindings, and queue
  observable UV, normal, normal-bake, and albedo-bake jobs through
  `DerivedJobRegistry` while GPU residency remains texture-handoff owned.
- [RUNTIME-113 — Progressive domain presentation extraction](../../done/RUNTIME-113-progressive-domain-presentation-extraction.md)
  (done, 2026-06-16, `CPUContracted`): runtime extraction consumes progressive
  presentation descriptors for mesh surface slots, graph vertex/edge property
  buffers, point-cloud property buffers, diagnostics, and previous-output
  retention without blocking on derived jobs.
- [RUNTIME-112 — Entity derived-job graph and snapshots](../../done/RUNTIME-112-entity-derived-job-graph.md)
  (done, 2026-06-16, `CPUContracted`): `StreamingExecutor`-backed derived-job
  registry now exposes entity/global snapshots, explicit dependencies,
  follow-up scheduling, stale/cancel/failure diagnostics, and previous-output
  retention.
- [RUNTIME-111 — Progressive render-data descriptor contracts](../../done/RUNTIME-111-progressive-render-data-descriptors.md)
  (done, 2026-06-16, `CPUContracted`): shared mesh/graph/point-cloud
  progressive descriptors, slot/source/readiness/generated-output policy,
  property compatibility diagnostics, and scene serialization now exist without
  raw property pointers or GPU handles.
- [RUNTIME-110 — Progressive entity render-data pipeline clarification](../../done/RUNTIME-110-progressive-entity-render-data-pipeline.md)
  (done, 2026-06-16, `Scaffolded`): accepted ADR-0021's progressive
  mesh/graph/point-cloud render-data model and split implementation into
  `RUNTIME-111` through `RUNTIME-114`, `UI-015`, and `GRAPHICS-090`.
- [RUNTIME-101 — Asset ingest state-machine migration](../../done/RUNTIME-101-asset-ingest-state-machine.md)
  (done, 2026-06-15, `CPUContracted`): promoted the runtime ingest
  request/result state machine for manual import, dropped files, and reimport;
  routed Engine import entry points through shared diagnostics and duplicate/
  stale guards; and kept reimport as same-`AssetId` `AssetService` reload
  without reviving ECS or scene-file asset-source coupling.
- [RUNTIME-108 — Remove mesh UV normal fallback](../../done/RUNTIME-108-resolved-uv-render-residency.md)
  (done, 2026-06-13, `CPUContracted`): mesh surface packing now requires
  count-matched finite `v:texcoord` and never substitutes oct-encoded normals
  into `MeshVertex::U/V`; missing or invalid texture coordinates fail closed
  with explicit runtime extraction diagnostics.
- [RUNTIME-105 — Remove the deprecated GetStreamingGraph() TaskGraph bridge](../../done/RUNTIME-105-remove-streaming-graph-bridge.md)
  (done, 2026-06-15, `Retired`): deleted the promoted runtime
  `Engine::GetStreamingGraph()` compatibility accessor, the private streaming
  task graph, and the per-frame TaskGraph-to-`StreamingExecutor` conversion.
  `StreamingExecutor` is now the only promoted runtime streaming path.
- [RUNTIME-103 — Geometry algorithm execution queue](../../done/RUNTIME-103-geometry-algorithm-execution-queue.md)
  (done, 2026-06-15, `CPUContracted`): value-gated the promoted editor
  geometry-processing path and retained synchronous CPU K-Means as the intended
  endpoint for current workflows. No runtime async algorithm queue or CUDA
  follow-up is owed without a new concrete workload.
- [RUNTIME-107 — Headless-capable Engine::Run loop coverage](../../done/RUNTIME-107-headless-engine-loop-coverage.md)
  (done, 2026-06-15, `Operational`): added an explicit
  `WindowBackend::Null` test-facing window backend selector and routed the
  BUG-030 `Engine::Run()` regressions through it so they execute on headless
  hosts instead of skipping.
- [GRAPHICS-084 — Visualization property-buffer residency](../../done/GRAPHICS-084-visualization-property-buffer-residency.md)
  (done, 2026-06-11, `CPUContracted`): consumed runtime visualization adapter/
  property selections while keeping GPU upload ownership in graphics.
- [RORG-031C — Runtime composition backlog seed](../../done/RORG-031C-runtime-composition.md)
  (done, 2026-06-10): composition-root and lifecycle backlog work for
  `begin_frame`, extraction, prepare, execute, end, shutdown determinism, and
  subsystem wiring; executed via `RUNTIME-099`/`RUNTIME-100`/`RUNTIME-102`
  with `RUNTIME-101` later retired as the independently tracked asset-ingest
  child.
- [RUNTIME-104 — Derived overlay producer lifecycle](../../done/RUNTIME-104-derived-overlay-producer-lifecycle.md)
  (done, 2026-06-11, `CPUContracted`): classified legacy
  `Graphics.OverlayEntityFactory` behavior for current workflows and retained no
  persistent runtime overlay producer API. Mesh/graph/point child overlays map
  to ordinary `GeometrySources` entities, mesh edge/vertex overlays use
  component-driven primitive-view sidecars, and vector-field/isoline overlays use runtime
  visualization packets without child ECS entities. Backend command-shape proof
  is retired by
  [GRAPHICS-085](../../done/GRAPHICS-085-overlay-packet-backend-parity.md).
- [RUNTIME-106 — Render component domain composition](../../done/RUNTIME-106-render-component-domain-composition.md)
  (done, 2026-06-12, `CPUContracted`): aligned mesh, graph, and point-cloud
  rendering around `RenderSurface`, `RenderEdges`, and `RenderPoints`
  component presence; mesh edge/vertex sidecars are now driven by render
  components rather than `MeshPrimitiveViewSettings`, and unsupported
  point-cloud surface/edge requests fail closed with diagnostics.
- [RUNTIME-091 — Activate promoted ECS system bundle in fixed-step runtime](../../done/RUNTIME-091-promoted-ecs-system-bundle-activation.md)
  (done): runtime-owned activation of promoted ECS systems via
  `Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle`, called
  every fixed-step substep before `Core::FrameGraph::Compile` so
  `TransformHierarchy` + `BoundsPropagation` run deterministically before
  render extraction.
- [RUNTIME-096 — Runtime module implementation splits](../../done/RUNTIME-096-runtime-module-implementation-splits.md):
  module-interface hygiene follow-up for promoted runtime `.cppm` targets found
  by the 2026-06-06 implementation-body audit, including camera controllers,
  gizmo helpers, engine helper functions, and geometry packers.
- [RUNTIME-097 — Default sandbox ECS-authored white triangle](../../done/RUNTIME-097-default-sandbox-ecs-triangle.md)
  (done, 2026-06-07, `CPUContracted`): replaced the default sandbox/reference
  triangle's `ProceduralGeometryRef` bootstrap with ordinary mesh-domain
  `GeometrySources`, selectable/editor-visible components, and a white
  appearance contract while keeping the sandbox app implementation runtime-only.
- [RUNTIME-098 — Promoted scene serialization and editor command seam](../../done/RUNTIME-098-promoted-scene-serialization.md)
  (done, 2026-06-07, `CPUContracted`): adds backend-neutral JSON scene
  save/load over current sandbox-authored ECS data, runtime `Engine` scene-file
  facades, and Sandbox editor `File / Scene` commands without reviving legacy
  serializer/editor modules.
- [RUNTIME-099 — Runtime lifecycle composition pipeline](../../done/RUNTIME-099-runtime-lifecycle-composition.md)
  (done, 2026-06-09, `CPUContracted`): `Engine::RunFrame()` carries an
  internal `RuntimeFrameContext` and delegates platform/render/maintenance/
  operational/shutdown phase ordering through promoted `Extrinsic.Core.FrameLoop`
  contracts, replacing legacy render orchestration with explicit runtime stage
  order and shutdown determinism.
- [RUNTIME-100 — Scene manager lifecycle and persistence boundary](../../done/RUNTIME-100-scene-manager-lifecycle.md)
  (done, 2026-06-09, `CPUContracted`): single runtime scene replacement
  boundary, render-extraction/selection/physics reset contracts, and explicit
  supported/deferred/retired persistence decisions beyond `RUNTIME-098`.
- [RUNTIME-102 — Editor command history and undo/redo seam](../../done/RUNTIME-102-editor-command-history.md)
  (done, 2026-06-09, `CPUContracted`): runtime/editor command history,
  dirty-state source, recursive delete/orphan policy, undo/redo contracts, and
  Sandbox editor document-state model.
- [RUNTIME-070 — Bootstrap GpuAssetCache fallback texture in Engine::Initialize](../../done/RUNTIME-070-fallback-texture-bootstrap.md):
  runtime-side graphics-bootstrap step initializing the canonical 4×4 magenta
  fallback texture per GRAPHICS-015Q (done).
- [BUILD-001 — Wire shader compilation to the promoted Sandbox build](../../done/BUILD-001-sandbox-shader-compile-wiring.md):
  CMake-only task adding `intrinsic_add_glsl_shaders(ExtrinsicSandbox)` so the
  promoted Sandbox build emits SPIR-V binaries; unblocks GRAPHICS-031A pipeline
  loads.
- [RUNTIME-081 — `Extrinsic.Runtime.CameraControllers`](../../done/RUNTIME-081-camera-controllers.md):
  Orbit / Fly / FreeLook / TopDown camera controllers producing
  `RenderFrameInput::Camera` (clarified by GRAPHICS-017Q; done).
- [RUNTIME-080 — `Extrinsic.Runtime.AssetBridges.Texture`](../../done/RUNTIME-080-asset-bridges-texture.md)
  _(superseded, retired 2026-06-03)_: texture-typed asset event subscriber
  producing `GpuAssetCache::RequestUpload` calls (clarified by GRAPHICS-015Q).
  The capability shipped under `ASSETIO-001` as
  `Extrinsic.Runtime.AssetModelTextureHandoff`; this umbrella was retired
  without re-implementation.
- [RUNTIME-082 — `Extrinsic.Runtime.SpatialDebugAdapters`](../../done/RUNTIME-082-spatial-debug-adapters.md)
  (done 2026-05-27): BVH / KD-tree / Octree / ConvexHull adapters producing
  spatial-debug snapshot records (clarified by GRAPHICS-011Q). All four
  slices landed (umbrella + BvhAdapter → KdTree + Octree adapters →
  ConvexHull adapter + registry → `ExtractAndSubmit` wiring +
  `ECS::Components::SpatialDebugBinding` + cache-owned adapters); retired
  to `tasks/done/`.
- [RUNTIME-083 — `Extrinsic.Runtime.VisualizationAdapters`](../../done/RUNTIME-083-visualization-adapters.md)
  (done 2026-06-02, `CPUContracted`):
  PropertySet / KMeans / Isoline / VectorField / HtexMetadata adapters producing
  visualization packet spans (clarified by GRAPHICS-014Q).
- [RUNTIME-084 — `Extrinsic.Runtime.GizmoInteraction`](../../done/RUNTIME-084-gizmo-interaction.md)
  (done 2026-06-06, `CPUContracted`): transform-gizmo hit testing,
  translate/rotate/scale drag application, undo emission, Engine
  input/camera/selection wiring, and
  `RuntimeRenderSnapshotBatch::TransformGizmos` submission (clarified by
  GRAPHICS-017Q).
- [RUNTIME-090 — `Extrinsic.Runtime.ImGuiAdapter`](../../done/RUNTIME-090-imgui-platform-renderer-adapter.md)
  (done, 2026-06-02, `CPUContracted`): Dear ImGui platform/renderer adapter
  producing `ImGuiOverlayFrame` records for `ImGuiOverlaySystem::SubmitFrame`
  (clarified by GRAPHICS-013CQ).
- [RUNTIME-085 — `GeometrySources` mesh residency bridge](../../done/RUNTIME-085-geometrysources-mesh-residency.md)
  (retired to `tasks/done/` 2026-05-28 at `CPUContracted`):
  runtime-authored ECS mesh data (`Vertices`/`Edges`/`Halfedges`/`Faces`) to
  retained `GpuWorld` surface geometry. Promoted from backlog 2026-05-27;
  Slice A landed the `Extrinsic.Runtime.MeshGeometryPacker` module (mesh
  `GeometrySources` → `GpuWorld::GeometryUploadDesc` triangle-list shape
  with fail-closed `MeshPackStatus` taxonomy), Slice B the extraction wiring,
  and Slice C the dirty-domain reupload + deferred-retire ordering.
  `Operational` visual proof is closed by RUNTIME-095.
- [RUNTIME-086 — `GeometrySources` graph residency bridge](../../done/RUNTIME-086-geometrysources-graph-residency.md):
  graph nodes/edges to retained point and line geometry. _(retired to
  `tasks/done/` on 2026-05-30 at maturity `CPUContracted`; Slice A — graph
  packer — plus Slices B + C — `RenderExtractionCache` residency wiring — all
  landed.)_
- [RUNTIME-087 — `GeometrySources` point-cloud residency bridge](../../done/RUNTIME-087-geometrysources-pointcloud-residency.md):
  point-cloud vertices to retained point geometry. _(retired to `tasks/done/` on
  2026-05-30 at maturity `CPUContracted`; standalone point-cloud packer plus
  `RenderExtractionCache` residency wiring, deferred-retire, and shutdown drain
  landed together.)_
- [RUNTIME-088 — Mesh primitive view lifecycle](../../done/RUNTIME-088-mesh-primitive-view-lifecycle.md)
  _(done 2026-05-31 at maturity `CPUContracted`: optional mesh edge/vertex render
  views as runtime sidecars over the authoritative mesh `GeometrySources`,
  wired into `RenderExtractionCache`; `Operational` visual proof closed by
  RUNTIME-095.)_
- [RUNTIME-089 — Runtime selection controller and snapshot handoff](../../done/RUNTIME-089-selection-controller.md)
  _(done; retired 2026-05-31 at `CPUContracted`)_: input/pick-result policy,
  selected/hovered state, and `RenderWorld.Selection` submission. Slice A
  (standalone `Extrinsic.Runtime.SelectionController` module) landed at
  `Scaffolded`; Slice B wired `Engine::RunFrame` (pick drain + readback consume)
  and `RenderExtractionCache::ExtractAndSubmit` (`RenderWorld.Selection` mirror)
  to close `CPUContracted`.
- [RUNTIME-092 — Runtime stable entity lookup sidecar](../../done/RUNTIME-092-stable-entity-lookup.md)
  _(done 2026-05-31, `CPUContracted`)_: runtime-owned `StableId`/live-entity
  lookup for selection and editor tooling. Slice A landed the standalone
  `Extrinsic.Runtime.StableEntityLookup` module (`HARDEN-068` Decision-3 deferred
  sidecar) with deterministic smallest-render-id duplicate policy and lazy stale
  invalidation; Slice B wired it into `Engine::RunFrame` (per-frame `Rebuild`
  before the pick-readback drain) and routed the `SelectionController` render-id
  resolution seam through the sidecar to close `CPUContracted`.
- [RUNTIME-093 — Primitive selection refinement](../../done/RUNTIME-093-primitive-selection-refinement.md)
  (done, 2026-06-01, `CPUContracted`): mesh face/edge/vertex, graph edge/node, and
  point-cloud point refinement from graphics primitive hints plus authoritative
  `GeometrySources`, wired into `Engine::RunFrame` via `RefinePickReadbackResult`.
- [RUNTIME-095 — Working sandbox app acceptance path](../../done/RUNTIME-095-working-sandbox-acceptance.md):
  done 2026-06-04 at `Operational` on Vulkan-capable hosts; final CPU/null +
  opt-in Vulkan acceptance for mesh, graph, point cloud, camera, selection,
  outline, and UI.
- [RUNTIME-097 — Default sandbox ECS-authored white triangle](../../done/RUNTIME-097-default-sandbox-ecs-triangle.md)
  _(done 2026-06-07 at maturity `CPUContracted`)_: the default visible triangle
  is an ordinary ECS mesh entity using the same runtime extraction and editor
  inspection paths as loaded mesh objects.
- [RUNTIME-098 — Promoted scene serialization and editor command seam](../../done/RUNTIME-098-promoted-scene-serialization.md)
  _(done 2026-06-07 at maturity `CPUContracted`)_: scene save/load persists
  current sandbox-authored mesh/graph/point-cloud ECS data and is exposed
  through the runtime-owned Sandbox editor scene-file command surface.
- [GRAPHICS-016 — Runtime extraction and graphics handoff](../../done/GRAPHICS-016-runtime-extraction-handoff.md):
  - Runtime owns live ECS access, extraction, sidecar/cache mappings from ECS
    entities and asset/source handles to graphics handles, dirty-domain
    interpretation, deletion events, and compaction/relocation handoff.
  - Graphics must not import live ECS ownership; promoted graphics layers
    consume snapshots/views only.
  - GRAPHICS-016 completed the first implementation gate for the rendering
    backlog before most rendering pass implementation work begins. See
    the rendering DAG in
    [`tasks/backlog/rendering/README.md`](../rendering/README.md) for
    downstream ordering.
- [`GRAPHICS-001 — Rendering parity inventory and task index`](../../done/GRAPHICS-001-rendering-parity-inventory.md) —
  retired rendering parity seed; current rendering selection lives in the
  rendering backlog DAG above.

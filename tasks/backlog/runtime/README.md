# Runtime Backlog Index

This README is the agent-facing entry point into the `tasks/backlog/runtime/`
queue. It lists runtime-owned backlog work and cross-links rendering tasks
whose ownership lives in `src/runtime` even when the task file is filed under
another backlog directory.

## Runtime backlog tasks

- [RORG-031C — Runtime composition backlog seed](RORG-031-runtime-composition.md):
  composition-root and lifecycle backlog work for `begin_frame`, extraction,
  prepare, execute, end, shutdown determinism, and subsystem wiring.
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
- [RUNTIME-099 — Runtime lifecycle composition pipeline](RUNTIME-099-runtime-lifecycle-composition.md):
  implementation child for `RORG-031C`, replacing legacy render orchestration
  with explicit runtime stage order and shutdown determinism.
- [RUNTIME-100 — Scene manager lifecycle and persistence boundary](RUNTIME-100-scene-manager-lifecycle.md):
  world reset, sidecar cleanup, stable-identity rebuild, and supported/deferred/
  retired persistence decisions beyond `RUNTIME-098`.
- [RUNTIME-101 — Asset ingest state-machine migration](RUNTIME-101-asset-ingest-state-machine.md):
  promoted replacement for legacy `Runtime.AssetIngestService` over
  `AssetService`, `StreamingExecutor`, and runtime materialization handoffs.
- [RUNTIME-102 — Editor command history and undo/redo seam](../../done/RUNTIME-102-editor-command-history.md)
  (done, 2026-06-09, `CPUContracted`): runtime/editor command history,
  dirty-state source, recursive delete/orphan policy, undo/redo contracts, and
  Sandbox editor document-state model.
- [RUNTIME-103 — Geometry algorithm execution queue](RUNTIME-103-geometry-algorithm-execution-queue.md):
  value-gated asynchronous CPU geometry processing and CUDA keep/defer/remove
  decision for legacy K-Means behavior.
- [RUNTIME-104 — Derived overlay producer lifecycle](RUNTIME-104-derived-overlay-producer-lifecycle.md):
  conditional runtime-owned persistent overlay descriptors, stable keys, parent
  closure, and extraction packet production when existing packet lanes are not
  sufficient.

### Sandbox / triangle path support tasks (runtime-owned)

Completed support tasks:
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

### Runtime adapter umbrellas (clarified by Q tasks; producer modules)

These open the runtime-side producer/owner umbrellas already clarified by the
done-task `Q` follow-ups. Each unblocks one or more rendering pass families.

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

### Working sandbox runtime path

These tasks fill the runtime-owned gaps between the renderer pass DAG and a
usable sandbox that can show authored mesh, graph, and point-cloud data with
selection and UI. They are ordered after the visible-triangle foundation and
compose with the rendering tasks listed in `tasks/backlog/rendering/README.md`.

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

## Cross-linked rendering tasks (runtime-owned)

Some rendering backlog tasks are runtime-owned for extraction/wiring even
though they may be filed under another task queue. Runtime reviewers must treat
these as runtime work when scheduling and review:

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
- [GRAPHICS-084 — Visualization property-buffer residency](../rendering/GRAPHICS-084-visualization-property-buffer-residency.md):
  consumes runtime visualization adapter/property selections but keeps GPU
  upload ownership in graphics.
- [GRAPHICS-085 — Overlay packet backend parity](../rendering/GRAPHICS-085-overlay-packet-backend-parity.md):
  backend proof for packets produced by `RUNTIME-104`.

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`tasks/backlog/rendering/README.md`](../rendering/README.md) — rendering
  backlog DAG and selection rules.
- [`GRAPHICS-001 — Rendering parity inventory and task index`](../../done/GRAPHICS-001-rendering-parity-inventory.md) —
  retired rendering parity seed; current rendering selection lives in the
  rendering backlog DAG above.

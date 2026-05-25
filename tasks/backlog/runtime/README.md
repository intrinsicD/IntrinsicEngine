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

- [RUNTIME-080 — `Extrinsic.Runtime.AssetBridges.Texture`](RUNTIME-080-asset-bridges-texture.md):
  texture-typed asset event subscriber producing `GpuAssetCache::RequestUpload`
  calls (clarified by GRAPHICS-015Q).
- [RUNTIME-082 — `Extrinsic.Runtime.SpatialDebugAdapters`](../../active/RUNTIME-082-spatial-debug-adapters.md)
  (active 2026-05-25): BVH / KD-tree / Octree / ConvexHull adapters producing
  spatial-debug snapshot records (clarified by GRAPHICS-011Q). Promoted to
  active with a four-slice plan (umbrella + BvhAdapter → KdTree + Octree
  adapters → ConvexHull adapter + registry → extraction wiring); Slice A
  in-progress on `claude/intrinsicengine-agent-onboarding-k31Vm`.
- [RUNTIME-083 — `Extrinsic.Runtime.VisualizationAdapters`](RUNTIME-083-visualization-adapters.md):
  PropertySet / KMeans / Isoline / VectorField / HtexMetadata adapters producing
  visualization packet spans (clarified by GRAPHICS-014Q).
- [RUNTIME-084 — `Extrinsic.Runtime.GizmoInteraction`](RUNTIME-084-gizmo-interaction.md):
  transform-gizmo hit testing, interaction state, drag application, undo
  emission (clarified by GRAPHICS-017Q).
- [RUNTIME-090 — `Extrinsic.Runtime.ImGuiAdapter`](RUNTIME-090-imgui-platform-renderer-adapter.md):
  Dear ImGui platform/renderer adapter producing `ImGuiOverlayFrame` records for
  `ImGuiOverlaySystem::SubmitFrame` (clarified by GRAPHICS-013CQ).

### Working sandbox runtime path

These tasks fill the runtime-owned gaps between the renderer pass DAG and a
usable sandbox that can show authored mesh, graph, and point-cloud data with
selection and UI. They are ordered after the visible-triangle foundation and
compose with the rendering tasks listed in `tasks/backlog/rendering/README.md`.

- [RUNTIME-085 — `GeometrySources` mesh residency bridge](RUNTIME-085-geometrysources-mesh-residency.md):
  runtime-authored ECS mesh data (`Vertices`/`Edges`/`Halfedges`/`Faces`) to
  retained `GpuWorld` surface geometry.
- [RUNTIME-086 — `GeometrySources` graph residency bridge](RUNTIME-086-geometrysources-graph-residency.md):
  graph nodes/edges to retained point and line geometry.
- [RUNTIME-087 — `GeometrySources` point-cloud residency bridge](RUNTIME-087-geometrysources-pointcloud-residency.md):
  point-cloud vertices to retained point geometry.
- [RUNTIME-088 — Mesh primitive view lifecycle](RUNTIME-088-mesh-primitive-view-lifecycle.md):
  optional mesh edge/vertex render views for primitive visualization and later
  primitive selection.
- [RUNTIME-089 — Runtime selection controller and snapshot handoff](RUNTIME-089-selection-controller.md):
  input/pick-result policy, selected/hovered state, and `RenderWorld.Selection`
  submission.
- [RUNTIME-092 — Runtime stable entity lookup sidecar](RUNTIME-092-stable-entity-lookup.md):
  runtime-owned `StableId`/live-entity lookup for selection and editor tooling.
- [RUNTIME-093 — Primitive selection refinement](RUNTIME-093-primitive-selection-refinement.md):
  mesh face/edge/vertex, graph edge/node, and point-cloud point refinement from
  graphics primitive hints plus authoritative `GeometrySources`.
- [RUNTIME-095 — Working sandbox app acceptance path](RUNTIME-095-working-sandbox-acceptance.md):
  final CPU/null + opt-in Vulkan acceptance for mesh, graph, point cloud,
  camera, selection, outline, and UI.

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

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`tasks/backlog/rendering/README.md`](../rendering/README.md) — rendering
  backlog DAG and selection rules.
- [`tasks/backlog/rendering/GRAPHICS-001-rendering-parity-inventory.md`](../rendering/GRAPHICS-001-rendering-parity-inventory.md) —
  canonical rendering backlog index.

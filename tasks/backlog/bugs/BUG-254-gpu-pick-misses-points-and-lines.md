---
id: BUG-254
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and the Vulkan smoke run
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# BUG-254 — A click on a point or edge line with no surface behind it misses on Vulkan

## Goal
- On Vulkan, a click on any drawn primitive (surface, edge line, point), including one with
  no surface behind it, hits its entity and resolves to the clicked element in Entity mode and
  in the element modes that apply to it (Vertex for points, Edge for edge lines), so points,
  edges, faces and entities can all be selected; this includes primitives drawn from bound
  positions (UI-083 slice 3a).

## Context
- Found 2026-10-25 while writing the UI-083 slice 3a smoke (host `hochvogel`, NVIDIA GeForce
  RTX 4090, driver 580.159.04, `build/ci-vulkan`): the ReferenceTriangle's points lane drawn at
  positions shifted by +1 in x, Vertex mode, click on the shifted apex point (background
  around it). Selection diagnostics: `clicks=1 drained=1 readbacks=1 hits=0 noHits=1`; no
  refined primitive. The same smoke shows the points drawn at the shifted pixels, so the click
  pixel was on a visible point.
- Code facts (blockers for a point or edge line with no surface behind it):
  - The entity, face, edge and point ID pipelines test `DepthOp::Equal` without depth writes
    (`src/graphics/renderer/Graphics.Renderer.cpp`, the selection pipeline descriptors), against
    the depth prepass, which draws only the `SurfaceOpaque` bucket
    (`src/graphics/renderer/Passes/Pass.DepthPrepass.cpp`). A point or line fragment over
    background therefore fails the depth test, although `selection/point_id.frag` and
    `selection/edge_id.frag` write the entity id as well as the primitive id.
  - `PointIdPass` draws the `SelectionPoints` bucket, which `assets/shaders/instance_cull.comp`
    fills only for instances with `GpuRender_Selectable`; no runtime code sets that flag
    (`BuildRenderFlags` in `Runtime.RenderExtraction.cpp`, the primitive-view flags in
    `Runtime.RenderExtraction.Geometry.cpp`). `EdgeIdPass` draws the ordinary `Lines` bucket.
  - A probe that set `GpuRender_Selectable` on the mesh primitive views still read back no hit
    (reverted, not committed): the flag alone is not enough, consistent with the depth test.
  - Edge or point fragments that lie on a surface can pass the depth-equal test, and a surface
    hit already refines to a vertex or an edge; those clicks are not reported broken.
  - GRAPHICS-158 records that `selection/edge_id.vert` draws native lines instead of the visible
    line quads and `selection/point_id.vert` uses one uniform size instead of per-point sizes.
- No GPU smoke clicks a point or an edge line; `RuntimeSandboxAcceptanceGpuSmoke` covers surface
  face picks only (BUG-026B). Not checked: graph-only and point-cloud-only entities (the same
  cause predicts that their points and lines do not hit; not run).
- The CPU side already maps hits back: refinement measures against the positions of the hit
  lane and reports canonical element ids (UI-083 slice 3a).
- Relation: GRAPHICS-158 slice 4 asks for the same click evidence for rebound positions; this
  bug owns making the click hit. UI-083 then highlights a selection on every drawn lane that
  shows it (operator decision 2026-10-25).

## Acceptance criteria
- [ ] A click on a point with no surface beneath (mesh vertex view, graph node, point cloud
      point) hits its entity and resolves to that element in Vertex mode and to the entity in
      Entity mode.
- [ ] A click on an edge line with no surface beneath (mesh edge view, graph edge) hits its
      entity and resolves to that edge in Edge mode.
- [ ] The pick footprint matches what is drawn: the visible point size (uniform or per point)
      and line width.
- [ ] A point drawn from a bound Position (UI-083) is picked where it is drawn and resolves to
      its canonical vertex id (extends `RuntimeSandboxAcceptanceGpuSmoke.BoundPointPositionsDrawWhileTheSurfaceStaysCanonical`).
- [ ] Surface face picks (BUG-026B) keep passing; each case above has a `gpu;vulkan` smoke cited
      as run, with host GPU and driver.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests IntrinsicGraphicsVulkanSmokeTests -j 6
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120 \
  -R '^RuntimeSandboxAcceptanceGpuSmoke\.'
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests -j 6
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(SelectionSystemContracts|GraphicsCullingContracts|PrimitiveSelectionRefinementWiring|SceneInteractionModule)\.'
python3 tools/agents/check_task_policy.py --root . --strict
```

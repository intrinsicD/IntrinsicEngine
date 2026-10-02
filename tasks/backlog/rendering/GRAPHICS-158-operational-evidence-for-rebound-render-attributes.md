---
id: GRAPHICS-158
theme: J
depends_on: [RUNTIME-315]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
maturity_target: Operational
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources]
---
# GRAPHICS-158 — Rebound position, normal, size and width streams render correctly on Vulkan

## Goal
- Prove, and where needed repair, the graphics side of `RUNTIME-315`: a position, normal, color, point-size or line-width stream sourced from a user-chosen property is uploaded, culled, depth-tested and read back correctly on the promoted Vulkan path, including while a GPU method owns the property's output ring.

## Non-goals
- No `Vk*` types outside the RHI; no new binding model (`RUNTIME-315`); no material texture sources (`GRAPHICS-105`).

## Context
- Operator decisions 2026-10-02 (model unification, picking/culling follow displayed positions, canonical normals, single Color mechanism, pixel sizes) are recorded in [RUNTIME-315](../../done/RUNTIME-315-per-domain-render-attribute-source-binding.md#operator-decisions-2026-10-02).
- The renderer-side upload is already name-agnostic (`GeometryUploadDesc::PositionBytes`, `NormalBytes`, `PackedVertexColors`, `GpuEntityPointConfig::PointSizeBDA`, `src/graphics/rhi/RHI.Types.cppm:185`); the open graphics questions are the position preview/ring front (`Graphics.GpuWorld.*`, GRAPHICS-156: keyed to the canonical position property today), entity local bounds for culling when the packed positions change, and point/line shaders reading size/width from a bound buffer. Existing Sandbox GPU readback smokes live in `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`.
- Slice 1 starts with an audit; if the graphics layer needs no change, record that and ship only the evidence slices.
- RUNTIME-315 (CPUContracted, 2026-10-02) already wired the runtime and CPU-side graphics pieces this task must prove on Vulkan: `ResolveDisplayedPositions` feeds the plan builders, the position-front key (`ObservePositionFront` asks for the displayed property) and instance culling bounds (displayed AABB); named sizes/widths upload as `<id>:point_size|line_width:<name>` visualization property buffers whose addresses `VisualizationSyncSystem` writes to `PointSizeBDA`/`LineWidthBDA`, including lanes without a material or overlay through `VisualizationSyncRecord::LaneConfig`. CPU coverage: `PositionPreviewExtraction`, `MeshGeometryExtraction`, `GraphGeometryExtraction`, `RuntimeRenderExtraction.NamedPointSizeAndLineWidth*`/`MeshEdgeAndVertexViewLanes*`, `PrimitiveSelectionRefinementWiring.BoundPositions*`. `GpuWorld::GetBoundsForTest` exposes instance bounds.

## Maturity
- Target `Operational`; closes with an actually-run `gpu;vulkan` smoke on a Vulkan-capable host (the desktop seat is locked, so run under the nested Xephyr X server).

## Slice plan
1. **Audit and CPU contract (~250 lines).** Trace position/normal/size/width bytes to GPU buffers and entity bounds; add `tests/contract/graphics` cases that a changed position span updates `GpuBounds` and the position-preview key follows the bound property.
2. **Position ring and preview keyed by binding (~300 lines).** Resident position front for a bound property, stale/`BindingGeneration` rejection, no frame observes a half-published ring.
3. **Point size/line width from a bound buffer (~300 lines).** Shader/config path for per-element size and width; pixel-size semantics preserved (model-space units stay `RUNTIME-222`).
4. **Operational smoke (~350 lines).** `gpu;vulkan` readback: mesh, graph and point cloud rendered with an offset `vec3` position property land at the offset pixel location, a click pick at that pixel returns the right element id, camera-frustum culling uses the displayed bounds, validation layer clean.

## Acceptance criteria
- [ ] Rebound positions render at the displayed location with correct culling bounds and depth.
- [ ] Bound normal/color/size/width streams are read back at the expected pixels.
- [ ] No stale or half-published buffer is observed while a GPU method republishes a bound property.
- [ ] Vulkan validation reports no new errors; smoke results are cited from an actually-run Xephyr session.

## Verification
```bash
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'GpuWorld|GpuPropertyResidency' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

---
id: GRAPHICS-093
theme: none
depends_on: []
---
# GRAPHICS-093 — Define forward-line quad topology for retained GpuScene lines

## Completion
- Completed: 2026-06-18. Commit/PR: this retirement commit.
- Maturity: `CPUContracted`.
- Fix summary: appended a non-indexed `GpuDrawBucketKind::LineQuads` culling
  bucket for retained forward lines, emitted six vertices per line segment from
  the culling shader, switched `Pass.Forward.Line` to
  `DrawIndirectCount()`/`Topology::TriangleList`, and updated
  `forward/line.vert` to expand one-pixel screen-space quads from managed line
  endpoint indices.
- Selection boundary: the indexed `Lines` bucket remains intact for edge-id
  selection, so primitive-id behavior stays deterministic and no native wide-line
  dependency is introduced.
- Evidence: focused `glslc` compile for `assets/shaders/culling/instance_cull.comp`
  and `assets/shaders/forward/line.vert`; `cmake --preset ci`;
  `cmake --build --preset ci --target IntrinsicTests`; focused
  line/culling/selection/frame-recipe CTest; default CPU CTest gate
  (`2930/2930`); shader-output check; layering, test-layout, doc-link, task,
  session-brief, docs-sync, and whitespace checks.
- Follow-up: dynamic `GpuEntityConfig::Line.LineWidth` and `Line.LineWidthBDA`
  population/consumption remain owned by `GRAPHICS-092`.

## Goal
- Establish the promoted forward-line draw topology needed for screen-space thick
  lines, so `GRAPHICS-092` can consume `GpuEntityConfig::Line.LineWidth` /
  `Line.LineWidthBDA` without relying on native Vulkan wide lines or an impossible
  vertex-only expansion over the current indexed `LineList` pass.

## Non-goals
- No line-width styling beyond making the draw topology capable of shader-expanded
  quads.
- No color/colormap resolution changes; those remain owned by `GRAPHICS-091`.
- No changes to selection edge-id rendering unless the selected topology requires
  an explicit compatibility decision.
- No changes to legacy retained line rendering or visualization overlay packet lines.

## Context
- Owning subsystem/layer: `graphics/renderer` pass and culling contracts, with shader
  assets under `assets/shaders/forward/` and draw bucket structs in
  `src/graphics/rhi/RHI.Types.cppm`.
- Pre-fix state verified 2026-06-18: `Pass.Forward.Line` consumed the indexed
  `GpuDrawBucketKind::Lines` bucket through `DrawIndexedIndirectCount()`, bound the
  managed index buffer, and used `Topology::LineList`. The culling shader emitted
  `geo.LineIndexCount`, `geo.LineFirstIndex`, and `geo.VertexOffset` into that
  indexed bucket.
- `GRAPHICS-092` Slice B asks `forward/line.vert` to expand each line segment to a
  screen-space quad. A vertex shader receiving an indexed `LineList` invocation sees
  only the two endpoint vertices and cannot synthesize the four/six triangle vertices
  required for a quad without a corresponding non-indexed expanded draw or an expanded
  index topology.
- The accepted topology must preserve the existing selection edge-id path or document a
  separate selection-compatible bucket, because `Pass.Selection.EdgeId` currently reuses
  the indexed `Lines` cull bucket.

## Required changes
- [x] Decide and document the promoted thick-line topology: an expanded non-indexed
      line-quad bucket, an expanded index buffer, or another backend-portable contract
      that does not require native wide-line support.
- [x] Update the RHI/culling bucket contract for retained forward lines if the chosen
      topology is no longer the current indexed `LineList`.
- [x] Update `Pass.Forward.Line`, pipeline descriptor creation, and related pass
      contract tests to consume the chosen topology.
- [x] Preserve or explicitly split the selection edge-id bucket so primitive-id
      encoding remains deterministic.
- [x] Leave `GpuEntityConfig::Line` width population/consumption to `GRAPHICS-092`
      after this topology blocker lands.

## Tests
- [x] Add/extend `contract;graphics` coverage for the new forward-line command shape
      (draw kind, bucket kind, push constants, index-buffer binding policy).
- [x] Add/extend culling contract coverage for the chosen line bucket emission.
- [x] Add shader-compile coverage for any new or modified forward-line shader inputs.
- [x] Preserve the default CPU gate.

## Docs
- [x] Update `src/graphics/renderer/README.md` and the rendering backlog index with
      the chosen forward-line topology and its relationship to selection edge-id.
- [x] Regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] Forward retained lines have a backend-portable draw topology that can support
      screen-space quad expansion in `GRAPHICS-092`.
- [x] Selection edge-id rendering remains deterministic or has an explicitly documented
      split bucket.
- [x] No native wide-line dependency is introduced.
- [x] Default CPU gate stays green.
- [x] Layering preserved: renderer/RHI contracts change without importing runtime/ECS
      ownership into graphics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' \
  -R 'Line|Culling|Selection' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Depending on native wide-line rasterization as the portability path.
- Changing color/scalar/colormap visualization behavior.
- Touching legacy retained line rendering or visualization overlay packet lines.
- Adding live ECS/runtime/asset ownership to graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted`; `Operational` line-width rendering remains owned by
  `GRAPHICS-092` after this topology blocker lands.
- This task closes the line draw-topology blocker only. `Operational` owned by
  `GRAPHICS-092`.

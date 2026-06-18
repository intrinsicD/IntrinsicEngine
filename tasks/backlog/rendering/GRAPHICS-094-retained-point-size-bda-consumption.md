---
id: GRAPHICS-094
theme: none
depends_on: []
---
# GRAPHICS-094 — Consume per-point size BDA in retained point shader

## Goal
- Make named `RenderPoints::SizeSource` buffers fully live on the retained
  GpuScene point path by having `assets/shaders/forward/point.vert` consume
  `GpuEntityConfig::Point.PointSizeBDA[point_id]` when present, falling back to
  uniform `Point.PointSize` otherwise.

## Non-goals
- No changes to point color/colormap resolution; `GRAPHICS-091` owns that path.
- No changes to line width residency; `GRAPHICS-092` owns retained line width.
- No new point/surfel normal-buffer residency, Gaussian splat work, or material
  texture sampling.
- No legacy point shader changes.

## Context
- Owning layer: `graphics/renderer` shader contract plus the existing
  `GpuEntityConfig::Point` storage in `graphics/rhi`.
- Current state verified 2026-06-18: `VisualizationSyncSystem` can populate
  `cfg.Point.PointSizeBDA` from a named `RenderPoints::SizeSource` buffer in
  `GpuSceneSlot`, but `assets/shaders/forward/point.vert` still clamps only the
  uniform `cfg.Point.PointSize`. Docs previously described per-point BDA
  consumption as complete; this task owns making that factual.
- Element indexing should match the retained point draw path:
  `sourceVertexIndex = gl_VertexIndex / 6u`, including the cull shader's
  `PointFirstVertex * 6u` first-vertex offset.

## Required changes
- [ ] Update `assets/shaders/forward/point.vert` to read
      `GpuFloatBufferRef(cfg.Point.PointSizeBDA).Data[sourceVertexIndex]` when
      the BDA is non-zero, with the same deterministic clamp used by uniform
      point size.
- [ ] Preserve uniform-size behavior and point modes.
- [ ] Keep the `GpuEntityConfig` C++/GLSL layout unchanged.

## Tests
- [ ] Add or extend CPU/null shader-source or config coverage asserting the
      retained point shader consumes `PointSizeBDA` and still falls back to
      uniform `PointSize`.
- [ ] Compile `assets/shaders/forward/point.vert` with `glslc`.
- [ ] Preserve the default CPU-supported CTest gate.

## Docs
- [ ] Update renderer/architecture docs to state that per-point size BDA is
      consumed by the retained forward point shader.
- [ ] Update `tasks/backlog/rendering/README.md` and regenerate
      `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] Uniform point-size rendering is unchanged.
- [ ] Named per-point size buffers in `GpuSceneSlot` affect retained point
      billboard diameter through `Point.PointSizeBDA`.
- [ ] No new graphics imports of runtime/ECS/assets or legacy shader paths.
- [ ] Default CPU gate stays green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
glslc -I assets/shaders assets/shaders/forward/point.vert -o /tmp/intrinsic-forward-point.vert.spv
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' \
  -R 'Point|Visualization|EntityConfig' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change `GpuEntityConfig` layout or add new push constants.
- Do not route retained point size through runtime/ECS-owned live state.
- Do not alter legacy point shaders or transient debug point packets.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Maturity
- Target: `CPUContracted`.
- This task closes once the retained point shader consumes the existing BDA
  field under shader compile and CPU/null contract coverage; no `Operational`
  follow-up is owed unless visual point-size readback coverage is opened as a
  separate smoke task.

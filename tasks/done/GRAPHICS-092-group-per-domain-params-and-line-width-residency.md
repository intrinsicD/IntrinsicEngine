---
id: GRAPHICS-092
theme: none
depends_on: []
---
# GRAPHICS-092 — Group per-domain params in `GpuEntityConfig` and add line-width residency

## Goal
- Restructure `GpuEntityConfig` so per-domain parameters live in clearly-separated
  named sub-blocks (point, line) instead of being flat-mixed into the shared config,
  and add line-width residency (`RenderEdges::WidthSource` → GPU) at parity with the
  existing point-size residency — so surface, line, and point share one config
  "venue" and diverge only in genuinely domain-specific parameters.

## Non-goals
- No change to the shared visualization color/scalar/colormap resolution
  (owned by `GRAPHICS-091`).
- No new visualization modes, material features, or shading models.
- No change to the legacy retained line path (`Graphics.Renderer.cpp` retained line
  draw) or the visualization overlay-packet `LineWidth`
  (`Graphics.VisualizationPackets`); this task is the modern GpuScene forward path only.
- No change to point rendering behavior beyond moving its fields into a named
  sub-block (behavior-preserving).

## Context
- Owning layer: `graphics/rhi` (the `GpuEntityConfig` POD in
  `src/graphics/rhi/RHI.Types.cppm`), its shader mirror
  `assets/shaders/common/gpu_scene.glsl`, and the modern forward line vertex shader
  `assets/shaders/forward/line.vert`. Runtime/graphics extraction populates the
  config (`Runtime.RenderExtraction`, `Graphics.VisualizationSyncSystem`).
- Current state after Slice B (verified 2026-06-18): `GpuEntityConfig`
  (`RHI.Types.cppm`, 128 bytes, `static_assert(sizeof == 128)` plus offset
  assertions) keeps shared visualization/color fields in the parent config and
  stores domain-specific settings in `cfg.Point` and `cfg.Line`. Point size has
  uniform shader consumption via `cfg.Point.PointSize`; named per-point size
  buffers populate `cfg.Point.PointSizeBDA` and are consumed by the retained point
  shader under `GRAPHICS-094`. `RenderEdges::WidthSource` now populates
  `cfg.Line.LineWidth` for uniform widths and `cfg.Line.LineWidthBDA` for named
  per-edge width buffers, and `forward/line.vert` consumes that config while
  expanding retained `LineQuads`.
- Operational proof landed 2026-06-18: `RenderEdges::WidthSource`
  (`Graphics.Component.RenderGeometry.cppm:84`, `variant<float,string>`) is authored,
  serialized (`Runtime.SceneSerialization`), editor-driven (`SandboxEditorUi`),
  populated into `GpuEntityConfig::Line`, consumed by the modern forward line shader,
  and covered by the opt-in `gpu;vulkan` runtime sandbox smoke on Vulkan-capable
  hosts. (The legacy retained line path and the overlay-packet `LineWidth` are
  separate and out of scope.)
- `GRAPHICS-093` retired the draw-topology blocker for Slice B: retained forward
  lines now consume the non-indexed `LineQuads` bucket through `DrawIndirectCount()`
  and `Topology::TriangleList`, while edge-id selection keeps the indexed `Lines`
  bucket. Slice B used that topology to wire `RenderEdges::WidthSource`
  population and `GpuEntityConfig::Line.LineWidth` / `Line.LineWidthBDA`
  consumption under CPU/null coverage; Vulkan operational proof remains open.
- One entity can be surface+line+point simultaneously (`RenderFlags` bits in
  `gpu_scene.glsl`), so per-domain params must be **grouped sub-blocks, not a union** —
  point and line params can be live at the same time.
- Layout constraint: changes must keep the C++ `GpuEntityConfig` and the GLSL mirror
  byte-identical (scalar block layout), update the `static_assert(sizeof == …)`, and
  keep shared-field semantics unchanged.
- Sibling: `GRAPHICS-091` unifies the *color/colormap resolution* across
  surface/line/point; this task unifies the *structural per-domain params + line
  width*. Both touch `gpu_scene.glsl` and `forward/line.vert`, so coordinate ordering
  (land this task's Slice A config grouping first, or rebase) to avoid layout churn.
  No hard dependency edge.

## Required changes
- [x] Regroup `GpuEntityConfig` per-domain fields into named sub-blocks in
      `RHI.Types.cppm`: a point block (`PointMode`, `PointSize`, `PointSizeBDA`) and a
      line block (`LineWidth`, `LineWidthBDA`); keep shared fields (attribute BDAs,
      scalar/colormap/isoline, `ColorSourceMode`, `ElementCount`, `UniformColor`)
      shared. Preserve shared-field semantics.
- [x] Add `LineWidth` (float, default `1.0`) and `LineWidthBDA` (per-edge width
      buffer) to the line block.
- [x] Mirror the new layout in `assets/shaders/common/gpu_scene.glsl` (byte-identical
      scalar block) and update the `static_assert(sizeof(GpuEntityConfig) == …)`.
- [x] Populate line-width residency in extraction/sync at parity with point size:
      uniform `RenderEdges::WidthSource` (float) → `cfg.Line.LineWidth`; named per-edge
      buffer → `cfg.Line.LineWidthBDA` (`Runtime.RenderExtraction` and/or
      `Graphics.VisualizationSyncSystem`, mirroring `PointSize`/`PointSizeBDA`).
- [x] Consume line width in `assets/shaders/forward/line.vert`: expand the segment to
      a screen-space quad (mirroring `forward/point.vert` size expansion) so uniform
      and per-edge widths render; keep a deterministic default when no width is set.
- [x] Update `forward/point.*` and any other readers to the regrouped field names
      (mechanical, behavior-preserving).

## Tests
- [x] CPU/null `contract;graphics` (or `contract;runtime`) coverage asserting
      extraction/sync writes `cfg.Line.LineWidth`/`cfg.Line.LineWidthBDA` from
      `RenderEdges::WidthSource` at parity with `PointSize`/`PointSizeBDA`.
- [x] A layout/parity check (the updated `static_assert` plus a GLSL-compile check)
      confirming C++ and shader `GpuEntityConfig` stay in sync after regrouping.
- [x] Opt-in `gpu;vulkan` smoke proving a thick / per-edge line renders with the
      configured width on the modern forward line path; skip/fail-closed without
      promoted Vulkan.
- [x] Preserve the default CPU gate and existing point-size/line coverage
      (behavior-preserving for points and surfaces).

## Docs
- [x] Update `src/graphics/renderer/README.md` and/or
      `docs/architecture/graphics.md` / `docs/architecture/rendering-target-architecture.md`
      to document the regrouped `GpuEntityConfig` (shared vs per-domain sub-blocks) and
      line-width residency at parity with point size.
- [x] Update `tasks/backlog/rendering/README.md`, cross-link `GRAPHICS-091`, and
      regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] `GpuEntityConfig` shared fields are unchanged in semantics; point/line params
      live in clearly-named sub-blocks; C++ and GLSL layouts match with an updated
      `static_assert`.
- [x] Uniform and per-edge line widths authored via `RenderEdges::WidthSource` are
      populated into `GpuEntityConfig::Line` and consumed by the modern forward line
      shader's `LineQuads` expansion under CPU/null contract coverage.
- [x] Point and surface rendering are unchanged (parity).
- [x] Default CPU gate stays green.
- [x] The new `gpu;vulkan` line-width smoke passes on a Vulkan-capable host or skips
      deterministically.
- [x] Layering preserved: no new graphics→ECS/runtime/asset edges.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' \
  -R 'EntityConfig|Line|Point|Visualization' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' \
  -R 'Line|Width' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Completion
- Retired on 2026-06-18 at maturity `Operational` on Vulkan-capable hosts
  (`CPUContracted` everywhere else).
- Slice C added
  `RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleMeshConfiguredLineWidthAndPointDrawLanesPresent`,
  which authors a 12 px `RenderEdges::WidthSource`, reads back
  `GpuEntityConfig::Line.LineWidth` / `LineWidthBDA`, confirms the edge/point draw
  lanes remain emitted, and samples the default-recipe backbuffer for the configured
  line overlay.
- Verification performed:
  `cmake --build --preset ci-vulkan --target IntrinsicTests -j 2`,
  `cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j 2`,
  and
  `ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'RuntimeSandboxAcceptanceGpuSmoke\.ReferenceTriangleMeshConfiguredLineWidthAndPointDrawLanesPresent' --timeout 120`
  passed on the local Vulkan-capable host.

## Forbidden changes
- Making per-domain params a union (an entity may be multi-domain).
- Changing shared `GpuEntityConfig` field semantics or the color/colormap resolution
  (owned by `GRAPHICS-091`).
- Touching the legacy retained line path or the overlay-packet `LineWidth`.
- Breaking C++↔GLSL layout parity or removing the size `static_assert`.
- Adding live ECS/`AssetService`/runtime imports to graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (line-width smoke);
  `CPUContracted` everywhere else.
- Slice plan:
  - **Slice A.** Regroup `GpuEntityConfig` into shared + point/line sub-blocks
    (including new defaulted `LineWidth`/`LineWidthBDA`), C++ + GLSL mirror +
    `static_assert`; mechanical reader updates. Behavior-preserving. Closes
    `Scaffolded → CPUContracted`.
  - **Slice B.** Line-width residency population (extraction/sync) +
    `forward/line.vert` width consumption on the `LineQuads` topology + CPU parity
    tests. Closes the CPU/null line-width contract.
  - **Slice C.** Opt-in `gpu;vulkan` line-width smoke for uniform and per-edge
    widths on the retained forward line path. Landed 2026-06-18 and closes
    `Operational` on Vulkan-capable hosts.

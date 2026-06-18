---
id: GRAPHICS-091
theme: none
depends_on: []
---
# GRAPHICS-091 — Unify scalar-field / colormap visualization across surface, line, and point passes

## Goal
- Bring the modern GpuScene forward line and point passes to colormap/visualization
  parity with the surface pass, so every `VisualizationConfig::ColorSource` mode
  (`Material`, `UniformColor`, `ScalarField`, `PerVertex/Edge/FaceBuffer`) resolves
  identically across surface, line, and point through a single shared
  color-resolution path.

## Non-goals
- No new visualization modes beyond the existing `VisualizationConfig::ColorSource` set.
- No changes to the legacy per-draw shaders (`assets/shaders/point_*.{vert,frag}`,
  top-level `assets/shaders/line.frag`/`point.frag`, `assets/shaders/surface.{vert,frag}`);
  those retire under legacy graphics removal (`LEGACY-008`).
- No geometry/UV/texture-bake work — this is per-element buffer + colormap (BDA)
  resolution, not texture baking.
- No isoline/vector-field overlay pass changes (separate overlay passes, `GRAPHICS-014Q`).
- No push-constant budget growth or vertex-format changes.

## Context
- Owning subsystem/layer: `graphics/renderer` (shaders + `VisualizationSyncSystem`),
  with the GpuScene shader contract in `assets/shaders/common/gpu_scene.glsl`.
  `runtime` owns the visualization config + extraction; graphics consumes snapshots,
  BDAs, and the colormap LUT only.
- Current asymmetry (verified 2026-06-18):
  - Surface: `assets/shaders/forward/default_debug_surface.frag` applies the
    scalar→colormap on the GPU from `GpuEntityConfig` (`ColormapID`,
    `ScalarRangeMin/Max`, binning/isoline fields) plus the per-element scalar
    property buffer.
  - Line/point: `assets/shaders/forward/line.vert` and `forward/point.vert`
    resolve `vColor` to only `cfg.UniformColor` (when `ColorSourceMode == 1`) or
    white; `forward/line.frag`/`forward/point.frag` consume that pre-resolved
    `vColor`. They never read the colormap, the scalar buffer, or per-element RGBA
    buffers. Net: `ScalarField` and per-element RGBA visualization render only on
    surfaces; lines/points fall back to uniform/white.
- `VisualizationSyncSystem` already writes the colormap fields into
  `GpuEntityConfig` for line/point records (`BuildEntityConfig`) and builds a
  SciVis override material, but the consuming branch is missing in the line/point
  shaders, so those writes are currently dead for the line/point domains.
- Stale documentation (must be corrected): the `Graphics.VisualizationSyncSystem`
  module header (`src/graphics/renderer/Graphics.VisualizationSyncSystem.cppm`
  ~lines 36-41) and `src/runtime/README.md` describe a "CPU-baking path for Line
  and Point passes" that bakes scalar/per-domain colours into a `vis_colors_baked`
  buffer in `GpuSceneSlot`. No such buffer or bake exists anywhere in the
  implementation; the comment is aspirational/stale and conflicts with the
  GPU-resolution direction of this task.
- Design decision (nonblocking default): drive line/point color resolution from the
  existing `GpuEntityConfig` color-source contract (`ColorSourceMode` ∈
  `{Material, Uniform, ScalarField, PerElementRgba}`) plus a shared GLSL helper in
  `common/gpu_scene.glsl`, mirroring how the surface frag already resolves colour —
  rather than routing line/point through the material-slot `MaterialTypeID`. This
  keeps one source of truth and reuses the per-element scalar/color BDA addressing.
  Confirm before implementing if material-slot routing is preferred instead.
- Scalar resolution stage: resolve per-fragment for parity with the surface path so
  colormap interpolation/binning is correct along line segments; points carry a
  single per-point scalar (flat). The per-element scalar/color buffer is addressed
  by element id via the GpuScene BDA, exactly as the surface path does.

## Required changes
- [ ] Add a shared color-resolution helper to `assets/shaders/common/gpu_scene.glsl`
      (e.g. `ResolveVisualizationColor(cfg, elementId, scalar, baseColor)`) implementing
      the full `ColorSourceMode` set: material/uniform passthrough, scalar→colormap
      (range + binning + optional isoline tint), and per-element RGBA.
- [ ] Refactor `forward/default_debug_surface.frag` to call the shared helper
      (behavior-preserving) so surface/line/point share one branch.
- [ ] Extend `forward/point.vert`/`forward/point.frag` to resolve `ScalarField` and
      per-element RGBA via the shared helper (per-point scalar/color from the GpuScene
      element buffer), not just `UniformColor`/white.
- [ ] Extend `forward/line.vert`/`forward/line.frag` similarly, interpolating the
      scalar across the segment and applying the colormap per-fragment.
- [ ] Ensure `GpuEntityConfig` / the GpuScene table exposes the per-element
      scalar/color buffer pointer(s) and element-index basis needed by line/point;
      extend the contract only if the existing surface fields are insufficient, and
      keep the push-constant budget unchanged.
- [ ] Make `VisualizationSyncSystem` populate the line/point color-source config
      (mode + scalar/color BDA) so the previously-dead colormap writes become live for
      the line/point domains; keep surface behavior unchanged.
- [ ] Correct the stale `vis_colors_baked` CPU-bake description in
      `Graphics.VisualizationSyncSystem.cppm` and `src/runtime/README.md` to reflect
      GPU-side resolution.

## Tests
- [ ] Add/extend CPU/null `contract;graphics` coverage asserting `VisualizationSyncSystem`
      writes equivalent color-source config (mode, colormap id, range, scalar/color BDA)
      for surface, line, and point records given the same `VisualizationConfig`.
- [ ] Add or extend a shader-compile check so the shared `gpu_scene.glsl` helper compiles
      for the surface, line, and point stages.
- [ ] Add opt-in `gpu;vulkan` smoke proving a scalar-field colormap renders on a line set
      and a point set (sampled pixel matches the colormap), mirroring the surface
      scalar-field path; skip / fail-closed without promoted Vulkan.
- [ ] Preserve the default CPU gate and existing `GraphicsVisualizationPackets` /
      `VisualizationSyncSystem` coverage.

## Docs
- [ ] Update `docs/architecture/graphics.md` and/or `src/graphics/renderer/README.md` to
      state that scalar-field/per-element color resolution is shared across
      surface/line/point via `common/gpu_scene.glsl`.
- [ ] Remove/replace the `vis_colors_baked` references and record the unified
      GPU-resolution contract.
- [ ] Update `tasks/backlog/rendering/README.md` and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] The same `VisualizationConfig` (a `ScalarField` with a colormap, and a per-element
      RGBA buffer) produces equivalent colour on surface, line, and point renderables.
- [ ] Surface visualization output is unchanged (parity) after the shared-helper refactor.
- [ ] No `vis_colors_baked` buffer or CPU scalar→color bake for line/point is introduced;
      resolution is GPU-side from the property/colormap BDAs.
- [ ] Default CPU gate stays green; the new `gpu;vulkan` smoke passes on a Vulkan-capable
      host or skips deterministically.
- [ ] Layering preserved: graphics consumes snapshots/BDAs/colormap LUT only; no live
      ECS/`AssetService`/runtime ownership added.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' \
  -R 'Visualization|Colormap|SciVis|Line|Point' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' \
  -R 'Visualization|Colormap|Line|Point' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Modifying legacy per-draw shaders or routing line/point through legacy push-constant
  color paths.
- Introducing a CPU scalar→color bake or a `vis_colors_baked` buffer.
- Introducing texture baking for line/point scalar fields (this is the buffer/colormap path).
- Adding live ECS/`AssetService`/runtime imports to graphics.
- Expanding the push-constant budget or changing vertex formats.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (opt-in `gpu;vulkan` line/point colormap
  smoke); `CPUContracted` everywhere else.
- Slice plan:
  - **Slice A.** Shared `gpu_scene.glsl` color-resolution helper + surface refactor
    (behavior-preserving). Preserves the CPU gate. Defers line/point wiring.
  - **Slice B.** Line/point shader resolution + `VisualizationSyncSystem` wiring +
    CPU `contract;graphics` parity tests + stale-doc correction. Closes
    `Scaffolded → CPUContracted`.
  - **Slice C.** Opt-in `gpu;vulkan` smoke for line/point scalar-field colormap.
    Closes `Operational`.

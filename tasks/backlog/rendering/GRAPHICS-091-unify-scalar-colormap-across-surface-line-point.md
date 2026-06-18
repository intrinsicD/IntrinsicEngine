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
- Current state after Slice B (verified 2026-06-18):
  - Surface after Slice A: the promoted forward and deferred surface shader
    pairs (`assets/shaders/forward/default_debug_surface.*` and
    `assets/shaders/deferred/gbuffer.*`) consume `GpuEntityConfig` through the
    shared helper in `assets/shaders/common/gpu_scene.glsl` for material,
    uniform, scalar-field, and per-element RGBA color sources. Vertex-domain
    scalar/color values are forwarded as interpolants; face-domain values are
    read in the fragment shader by `gl_PrimitiveID`.
  - Line/point after Slice B: `assets/shaders/forward/point.vert` resolves one
    scalar/color element per retained source point through the same helper.
    `assets/shaders/forward/line.vert` forwards endpoint or segment
    scalar/color values and `forward/line.frag` resolves scalar colormaps
    per-fragment so vertex-domain values interpolate along the segment while
    edge-domain values remain flat per segment.
- `VisualizationSyncSystem` already writes the colormap fields into
  `GpuEntityConfig` for line/point records (`BuildEntityConfig`) and builds a
  SciVis override material. Slice B made those `GpuEntityConfig` scalar/color
  fields live for retained line and point shaders under CPU/null coverage; the
  opt-in Vulkan pixel smoke remains open for Slice C.
- Stale documentation (corrected by Slice A): the `Graphics.VisualizationSyncSystem`
  module header (`src/graphics/renderer/Graphics.VisualizationSyncSystem.cppm`
  ~lines 36-41 before this slice) described a "CPU-baking path for Line and Point
  passes" that baked scalar/per-domain colours into a `vis_colors_baked` buffer in
  `GpuSceneSlot`. No such buffer or bake exists anywhere in the implementation;
  the comment was aspirational/stale and conflicted with the GPU-resolution
  direction of this task. Keep future docs on the GPU-side BDA path.
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
- Sibling: `GRAPHICS-092` unifies the *structural* side of the same venue (grouping
  per-domain params in `GpuEntityConfig` and adding line-width residency). Both tasks
  touch `assets/shaders/common/gpu_scene.glsl` and `forward/line.vert`; coordinate
  ordering (land `GRAPHICS-092` Slice A config grouping first, or rebase) to avoid
  `GpuEntityConfig` layout churn. No hard dependency edge.

## Required changes
- [x] Add a shared color-resolution helper to `assets/shaders/common/gpu_scene.glsl`
      (e.g. `ResolveVisualizationColor(cfg, elementId, scalar, baseColor)`) implementing
      the full `ColorSourceMode` set: material/uniform passthrough, scalar→colormap
      (range + binning + optional isoline tint), and per-element RGBA.
- [x] Wire `forward/default_debug_surface.*` and `deferred/gbuffer.*` to call the
      shared helper so promoted surface rendering has a real GpuScene color-source
      consumer before line/point parity work.
- [x] Extend `forward/point.vert`/`forward/point.frag` to resolve `ScalarField` and
      per-element RGBA via the shared helper (per-point scalar/color from the GpuScene
      element buffer), not just `UniformColor`/white.
- [x] Extend `forward/line.vert`/`forward/line.frag` similarly, interpolating the
      scalar across the segment and applying the colormap per-fragment.
- [x] Ensure `GpuEntityConfig` / the GpuScene table exposes the per-element
      scalar/color buffer pointer(s) and element-index basis needed by line/point;
      extend the contract only if the existing surface fields are insufficient, and
      keep the push-constant budget unchanged.
- [x] Make `VisualizationSyncSystem` populate the line/point color-source config
      (mode + scalar/color BDA) so the previously-dead colormap writes become live for
      the line/point domains; keep surface behavior unchanged.
- [x] Correct the stale `vis_colors_baked` CPU-bake description in
      `Graphics.VisualizationSyncSystem.cppm` to reflect GPU-side resolution.

## Tests
- [x] Add/extend CPU/null `contract;graphics` coverage asserting `VisualizationSyncSystem`
      writes equivalent color-source config (mode, colormap id, range, scalar/color BDA)
      for surface, line, and point records given the same `VisualizationConfig`.
- [x] Run focused shader compile coverage for the shared helper's promoted surface
      consumers (`forward/default_debug_surface.*`, `deferred/gbuffer.*`).
- [x] Extend shader-compile coverage once line/point stages call the shared helper.
- [ ] Add opt-in `gpu;vulkan` smoke proving a scalar-field colormap renders on a line set
      and a point set (sampled pixel matches the colormap), mirroring the surface
      scalar-field path; skip / fail-closed without promoted Vulkan.
- [x] Preserve the default CPU gate and existing `GraphicsVisualizationPackets` /
      `VisualizationSyncSystem` coverage.

## Docs
- [x] Update `src/graphics/renderer/README.md` to state that promoted surface
      scalar-field/per-element color resolution is shared through
      `common/gpu_scene.glsl`.
- [x] Update docs again when line/point parity lands so the shared surface/line/point
      claim is factual.
- [x] Remove/replace the `vis_colors_baked` references and record the unified
      GPU-resolution contract.
- [x] Update `tasks/backlog/rendering/README.md` and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] The same `VisualizationConfig` (a `ScalarField` with a colormap, and a per-element
      RGBA buffer) produces equivalent colour config on surface, line, and point
      renderables under CPU/null coverage; Vulkan pixel proof remains Slice C.
- [ ] Surface visualization output is unchanged (parity) after the shared-helper refactor.
- [x] No `vis_colors_baked` buffer or CPU scalar→color bake for line/point is introduced;
      resolution is GPU-side from the property/colormap BDAs.
- [x] Default CPU gate stays green.
- [ ] The new `gpu;vulkan` smoke passes on a Vulkan-capable host or skips
      deterministically.
- [x] Layering preserved: graphics consumes snapshots/BDAs/colormap LUT only; no live
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
  - **Slice A.** Shared `gpu_scene.glsl` color-resolution helper + promoted
    forward/deferred surface color-source wiring. Preserves the CPU gate and
    creates the factual surface baseline. Defers line/point wiring.
  - **Slice B.** Line/point shader resolution + `VisualizationSyncSystem` wiring +
    CPU `contract;graphics` parity tests + stale-doc correction. Landed under
    CPU/null coverage and closes `Scaffolded → CPUContracted`; `Operational` is
    owned by GRAPHICS-091 Slice C.
  - **Slice C.** Opt-in `gpu;vulkan` smoke for line/point scalar-field colormap.
    Closes `Operational`.
    - Smoke authored 2026-06-18
      (`RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleScalarFieldColormapResolvesOnLineAndPointLanes`
      in `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`),
      mirroring the GRAPHICS-092 line/point readback precedent in the same file.
      It hard-asserts `LinePass`/`PointPass` recording on the operational command
      stream and reads the per-line and per-point `GpuEntityConfig` back from the
      GPU entity-config buffer to assert the unified scalar-field colormap config
      (`ColorSourceMode == ScalarField`, Viridis `ColormapID`, range, vertex
      domain); the scalar-buffer residency and colormap-pixel checks emit precise
      host diagnostics. The fixture self-skips on non-Vulkan hosts via
      `BootstrapDefaultSandboxAppEngine`.
    - Status: authored and compile-targeted under `ci-vulkan`; the `Operational`
      pixel-proof still needs a `gpu;vulkan` run on a Vulkan-capable host. This
      session's cloud environment has no Vulkan driver, so the smoke is verified
      only to skip deterministically here — it is not yet recorded as
      `Operational`. The task remains in backlog until that host run lands.

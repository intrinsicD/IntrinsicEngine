---
id: UI-032
theme: G
depends_on: [BUG-059]
maturity_target: CPUContracted
completed: 2026-07-06
---
# UI-032 — Appearance scalar/isoline visualization controls

## Status
- Retired on 2026-07-06 at `CPUContracted` on branch
  `claude/curvature-viz-black-mesh-5mqnpz`.
- PR/commit: this retirement commit.
- Delivered: Appearance-panel "Scalar field" + "Isolines" control blocks
  (colormap combo over the six built-in LUTs, auto-range toggle, manual
  min/max clamp, bin count, isoline count/width/color, and up to eight
  explicit highlight isovalue rows with add/remove), a widened editor
  command/model surface that round-trips those fields, preset buttons that
  preserve previously configured styling and tuned ranges, a bounded
  `ScalarFieldConfig::Isolines::Values` component extension with scene
  serialization, `RHI::GpuEntityConfig` + `gpu_scene.glsl` explicit-isovalue
  slots (176-byte config), and fail-closed sync plumbing that skips
  non-finite isovalues.

## Goal
- Give the Appearance panel real control over scalar-field visualization:
  colormap selection, range clamping (auto/manual min/max), and isoline
  styling (count/spacing, width, color) plus explicit highlight isovalues,
  wired through the existing editor command → `VisualizationConfig` →
  entity-config → shader contract.

## Non-goals
- No new colormap palettes and no colormap texture-baking changes.
- No real contour-line geometry extraction for the overlay isoline lane
  (`Pass.VisualizationOverlay` placeholder stays as is).
- No per-lane (edges/points) control fan-out beyond what the existing
  surface-lane preset flow already targets.

## Context
- Previous behavior: the Scalar/Isolines preset buttons constructed a fresh
  `VisualizationConfig` with hardcoded defaults — colormap always reset to
  Viridis, range controls existed in the command surface but had no UI, and
  isoline count was fixed at 12 with unconfigurable width/color. Explicit
  highlight isovalues were unsupported end to end.
- Owning layers: `runtime` (editor UI + commands, serialization),
  `graphics/renderer` (`VisualizationConfig` component,
  `VisualizationSyncSystem`, `RHI::GpuEntityConfig`), `assets/shaders`
  (`gpu_scene.glsl` isoline helpers). GPU struct and GLSL struct stay
  layout-synchronized (static_asserts updated for the 176-byte layout).

## Required changes
- [x] Extend `SandboxEditorVisualizationConfigCommand`/model with colormap,
      isoline width/color, and bounded explicit isovalues; presets seed
      styling from the existing effective config and pass the current
      range/bin settings instead of defaults.
- [x] Add Appearance-panel controls (colormap combo, auto-range checkbox,
      min/max drags, bin count, isoline count/width/color, isovalue list)
      bound to the command surface via
      `MakeScalarVisualizationConfigCommandFromModel`.
- [x] Extend `RHI::GpuEntityConfig` + `assets/shaders/common/gpu_scene.glsl`
      with a bounded explicit-isovalue set (`IsoValuesA/B` + `IsoValueCount`)
      applied by `GpuVisualizationApplyExplicitIsoValues` in un-binned
      normalized scalar space.
- [x] Plumb the new fields through `VisualizationSyncSystem::BuildEntityConfig`
      with fail-closed clamping (non-finite values skipped, count bounded).
- [x] Extend `SameVisualizationConfig` and scene serialization so styling
      edits are detected and persist.

## Tests
- [x] `SandboxEditorUi.VisualizationPresetPreservesConfiguredScalarStyling`
      (command → lane-override round-trip + preset preservation).
- [x] `RuntimeRenderExtraction.MeshSurfaceLaneOverrideIsolinePresetReachesPreparedEntityConfig`
      extended to prove explicit isovalues (with one non-finite entry skipped)
      reach the prepared `GpuEntityConfig`.
- [x] Scene serialization round-trip extended with isovalue persistence.
- [x] Focused suites + default CPU-supported correctness gate.

## Docs
- [x] Update this task and regenerate `tasks/SESSION-BRIEF.md`.
- [x] `docs/architecture/rendering-target-architecture.md` `GpuEntityConfig`
      layout updated (176 bytes, isovalue block).

## Acceptance criteria
- [x] Colormap choice, range clamping, and isoline styling round-trip from
      the editor command surface to the prepared entity config.
- [x] Explicit isovalues render through the surface isoline helper with a
      bounded count and fail-closed validation.
- [x] Preset buttons no longer clobber previously configured styling.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction|SandboxEditorUi|VisualizationAdapters|RuntimeSceneSerialization' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
for s in $(grep -rl "gpu_scene.glsl" assets/shaders --include="*.vert" --include="*.frag" --include="*.comp"); do glslangValidator --target-env vulkan1.2 -P"#extension GL_GOOGLE_include_directive : enable" -Iassets/shaders "$s"; done
```

## Forbidden changes
- Breaking the `GpuEntityConfig` ↔ `gpu_scene.glsl` layout contract.
- Unbounded per-entity isovalue arrays in the config struct.
- Reintroducing live ECS knowledge into graphics.

## Maturity
- Target: `CPUContracted` — met. Config plumbing proven at the CPU/null seam;
  all 19 shaders including `gpu_scene.glsl` validate for Vulkan 1.2 via
  glslangValidator. A `gpu;vulkan` pixel smoke for the explicit-isovalue
  fragment path is a reasonable follow-up on a GPU host.

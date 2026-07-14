---
id: BUG-059
theme: G
depends_on: []
maturity_target: CPUContracted
completed: 2026-07-06
---
# BUG-059 — Curvature scalar/isoline visualization renders black end to end

## Status
- Retired on 2026-07-06 at `CPUContracted` on branch
  `claude/curvature-viz-black-mesh-5mqnpz`.
- PR/commit: this retirement commit.
- Diagnosis: the promoted UI→extraction→sync→config contract was verified
  healthy end to end with new regression tests that mirror the real editor
  flow (surface-lane override written by the Appearance preset buttons +
  `double`-typed `v:mean_curvature` + auto range + isoline preset + late
  property arrival + steady-state frames). The reproducible all-black modes
  were:
  1. **Heavy-tailed auto range** — raw min/max auto ranges let a handful of
     curvature spikes (degenerate slivers) own the range, normalizing the
     surface bulk to the colormap's darkest bin (Viridis t≈0 under the
     0.35 lighting floor reads as black).
  2. **Degenerate manual range** — a component `RangeMin == RangeMax` reached
     the prepared config unchanged; the shader's flat-range guard then pins
     every fragment to t=0.
  (The BUG-057/058 packet-range plumbing fixes from 2026-07-05 remain
  necessary; builds predating them render the same symptom.)
- Fix: `ComputeRange` now clamps auto ranges to the [2%, 98%] quantiles for
  fields with ≥64 samples (exact min/max below that; outliers saturate at the
  colormap ends), reported via the new
  `RobustAutoRangeClampedCount`/`VisualizationAdapterRobustAutoRangeClampedCount`
  counters; `VisualizationSyncSystem::BuildEntityConfig` sanitizes
  degenerate/non-finite ranges (packet range fallback, else ±0.5 expansion)
  so the shader never sees a range that normalizes everything to t=0.

## Goal
- Restore non-black scalar and isoline visualization for a UI-computed mean
  curvature property on a loaded mesh through the promoted editor → runtime
  extraction → renderer sync → shader-config path, with the failure diagnosed
  end to end rather than patched at one seam.

## Non-goals
- No new visualization UI controls in this task (owned by `UI-032`).
- No changes to the explicit `IsolineAdapter` overlay-packet lane placeholder.
- No runtime main-loop cleanup.

## Context
- Symptom: after computing curvature via the editor UI and clicking the
  Appearance panel's "Scalar" or "Isolines" preset for `v:mean_curvature`,
  the mesh renders black.
- Expected behavior: the selected scalar property resolves a valid scalar
  buffer (BDA + element count), a finite computed auto range, and a colormap
  ID in the prepared `GpuEntityConfig`, so the surface shader produces
  colormapped output (with isoline overprint when enabled).
- Impact: scalar/isoline visualization was unusable on real curvature data.
- Prior art: `BUG-057`/`BUG-058` fixed the packet→sync range plumbing at the
  entity-config seam with a base-component `float` fixture. The user-visible
  flow differs: the mesh Appearance window issues `Target=Surface` commands
  that write `VisualizationLaneOverrides::Surface`, the curvature job authors
  `v:mean_curvature` as a `double` property through the derived-job apply
  lane, and real curvature data is heavy-tailed.
- Owning layers: `runtime` (adapters, extraction), `graphics/renderer`
  (entity-config sync). No layering changes.

## Required changes
- [x] Build a deterministic CPU/null repro that mirrors the real flow:
      surface-lane override config + `double` curvature property + extraction
      + render prep, asserting the prepared `GpuEntityConfig`.
- [x] Diagnose which seam breaks with ranked hypotheses (lane overrides,
      double adaptation, packet key/domain matching, residency, config sync,
      shader-side range/BDA fallback were each verified healthy; the
      reproducible defects were auto-range outlier compression and the
      degenerate-manual-range pass-through).
- [x] Fix the smallest defect(s): robust quantile auto range in
      `Runtime.VisualizationAdapters` + degenerate-range sanitization in
      `Graphics.VisualizationSyncSystem::BuildEntityConfig`.
- [x] Preserve fail-closed diagnostics (non-finite packet drop, invalid-range
      counters, missing-source counters unchanged; new clamp counter added).

## Tests
- [x] `RuntimeRenderExtraction.MeshSurfaceLaneOverrideDoubleCurvatureReachesPreparedEntityConfig`
- [x] `RuntimeRenderExtraction.MeshSurfaceLaneOverrideIsolinePresetReachesPreparedEntityConfig`
- [x] `RuntimeRenderExtraction.MeshScalarVisualizationSurvivesLatePropertyAndSteadyStateFrames`
- [x] `RuntimeRenderExtraction.MeshScalarDegenerateManualRangeIsSanitizedInPreparedConfig`
- [x] `VisualizationAdapters.PropertyScalarAdapterClampsHeavyTailedAutoRange`
- [x] `VisualizationAdapters.PropertyScalarAdapterKeepsExactAutoRangeForSmallFields`
- [x] Focused visualization suites + default CPU-supported correctness gate.

## Docs
- [x] Update this task and regenerate `tasks/SESSION-BRIEF.md`.
- [x] No scalar-contract doc schema change; diagnostics gained one counter.

## Acceptance criteria
- [x] The repro is documented and covered by automated regression tests.
- [x] Prepared entity configs for the repro carry nonzero scalar BDA, the
      computed auto range, a valid colormap ID, and isoline metadata.
- [x] Fix preserves runtime/graphics layering.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction|VisualizationAdapters|SandboxEditorUi|RuntimeSceneSerialization|VisualizationPropertyBufferResidency|GraphicsMinimalAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Hiding the bug behind a uniform fallback color for scalar-field mode.
- Fixing symptoms in the shader while leaving the CPU contract broken.
- Mixing in the `UI-032` appearance-control feature work.

## Maturity
- Target: `CPUContracted` — met. The CPU/null contract seam reproduces and
  locks both fixed defects; no backend-specific root cause surfaced, so no
  `Operational` follow-up is owed. A live Vulkan sandbox check on a
  Vulkan-capable host remains a useful manual confirmation (this environment
  has no GPU).

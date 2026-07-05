---
id: BUG-058
theme: G
depends_on: [BUG-057]
maturity_target: CPUContracted
completed: 2026-07-05
---
# BUG-058 — Entity isolines render black

## Status
- Retired on 2026-07-05 at `CPUContracted` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- Root cause: the editor Isoline preset uses `VisualizationConfig` scalar-field
  settings on the selected entity. Those isoline shader settings shared
  BUG-057's missing prepared-config auto range, so valid scalar fields could be
  rendered through an unusable default range.
- Fix: the scalar-property regression now enables isolines and proves the
  prepared entity config receives nonzero scalar BDA, element count, computed
  scalar range, colormap ID, isoline count, isoline width, and isoline color.

## Goal
- Restore editor isoline visualization on sandbox entities so selected scalar
  fields produce valid scalar-shader isoline config instead of black output.

## Non-goals
- Do not add new isoline algorithms or UI modes.
- Do not clean up the runtime main loop in this bug fix.
- Do not duplicate the scalar-property extraction fix owned by `BUG-057`.
- Do not change the explicit `IsolineAdapter` overlay-packet lane; existing
  overlay packet and pass contracts continue to cover that separate path.

## Context
- Symptom: isolines do not work and render black.
- Expected behavior: selecting an isoline preset for a scalar property publishes
  a valid scalar-field entity config with computed scalar range and nonzero
  isoline metadata, resolving the scalar source through the same promoted
  property-buffer contract as scalar colormaps.
- Owning layers: `runtime` owns editor command routing, selected-property
  config setup, and render extraction; `graphics/renderer` owns validated
  scalar packet consumption, entity config sync, colormap setup, and shader
  output. The explicit overlay-packet lane remains separate.
- This task depends on `BUG-057` so isoline-specific diagnosis starts after the
  shared scalar data/range path is proven healthy.

## Required changes
- [x] Reproduce the isoline black-output symptom with a deterministic
      runtime/graphics test or a bounded sandbox capture.
- [x] Verify isoline command routing, scalar packet validation,
      property-buffer upload, entity config sync, and shader color state.
- [x] Fix the smallest defect that prevents finite scalar isolines from
      producing visible shader output.
- [x] Preserve fail-closed diagnostics for missing scalar sources, invalid
      domains, empty/flat ranges, and missing scalar config data.

## Tests
- [x] Add or extend regression coverage proving the promoted isoline path
      publishes valid scalar-shader isoline config for a selected scalar field.
- [x] Determine whether opt-in `gpu;vulkan` smoke or pixel-readback coverage is
      required. It was not required because the root cause reproduced at the
      CPU/null renderer config contract.
- [x] Run focused visualization/runtime extraction tests, then the
      default CPU-supported correctness gate unless the slice is explicitly
      deferred.

## Docs
- [x] Update this task and regenerate `tasks/SESSION-BRIEF.md`.
- [x] Update rendering/runtime docs only if the isoline config contract or
      diagnostics change.

## Acceptance criteria
- [x] Valid scalar isoline presets no longer render as all-black output at the
      verified contract seam.
- [x] Isoline diagnostics distinguish missing upstream scalar data from
      shader-config setup failures.
- [x] The fix preserves runtime/graphics layering and does not broaden editor or
      main-loop responsibilities.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests
build/ci/bin/IntrinsicRuntimeGraphicsCpuTests --gtest_filter='RuntimeRenderExtraction.MeshScalarPropertyBufferReachesPreparedEntityConfigWithAutoRange'
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction|GraphicsMinimalAcceptance.*Visualization|VisualizationAdapters' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'GraphicsMinimalAcceptance|RenderPrepPipeline|VisualizationPropertyBufferResidency|RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Masking the failure by drawing isolines with a constant fallback unrelated to
  the selected scalar field.
- Reintroducing live ECS/runtime ownership into graphics.
- Mixing runtime main-loop cleanup or unrelated UI reorganization into this
  bug fix.

## Maturity
- Target: `CPUContracted` for the runtime/graphics scalar-shader isoline config
  contract; no `Operational` follow-up is owed because the root cause was not
  backend-specific.

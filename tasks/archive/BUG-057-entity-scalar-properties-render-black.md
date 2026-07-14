---
id: BUG-057
theme: G
depends_on: []
maturity_target: CPUContracted
completed: 2026-07-05
---
# BUG-057 — Entity scalar properties render black

## Status
- Retired on 2026-07-05 at `CPUContracted` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- Root cause: automatic runtime scalar property-buffer extraction produced a
  validated `ScalarAttributePacket` with a resolved GPU address and computed
  auto range, but `VisualizationSyncSystem` only received the property-buffer
  address table. Prepared entity configs therefore kept the default scalar
  range `[0, 1]`, which could clamp selected scalar fields into black output.
- Fix: renderer prep now passes scalar packets into visualization sync, and
  scalar-field entity config construction copies the packet BDA, element count,
  and computed auto range when the selected scalar property matches the entity
  visualization record.

## Goal
- Restore scalar-property visualization on sandbox entities so finite scalar
  properties selected through the promoted editor/runtime path resolve to
  non-black colormap output.

## Non-goals
- Do not add new visualization UI modes.
- Do not clean up the runtime main loop in this bug fix.
- Do not fix isoline-specific overlay rendering here except for shared scalar
  extraction or property-buffer plumbing needed by this scalar path.

## Context
- Symptom: scalar properties do not show on entities; selecting a scalar
  property produces black rendering.
- Expected behavior: a selected scalar property on a renderable entity publishes
  valid scalar/property-buffer data, a finite scalar range, and colormap
  configuration consumed by the promoted surface/line/point shaders.
- Owning layers: `runtime` owns editor command routing, property selection, and
  render extraction; `graphics/renderer` owns visualization packet validation,
  entity config sync, material setup, and shader consumption. Graphics must not
  import live ECS/runtime ownership.
- BUG-052 generalized automatic visualization property-buffer extraction across
  mesh, graph, and point-cloud domains, so this task must diagnose the current
  regression rather than reintroducing mesh-only special cases.

## Required changes
- [x] Build a deterministic repro for scalar-property visualization resolving
      to black, preferably at the runtime extraction or renderer contract seam.
- [x] Verify whether scalar property data, scalar range, property-buffer source
      keys, colormap IDs, and entity visualization config reach the renderer.
- [x] Fix the smallest runtime/graphics/shader contract defect that causes
      finite scalar values to resolve as black.
- [x] Preserve fail-closed diagnostics when scalar sources are missing,
      unsupported, empty, non-finite, or count-mismatched.

## Tests
- [x] Add or extend regression coverage proving an entity scalar property
      publishes valid scalar visualization data and range through the promoted
      runtime/graphics path.
- [x] Add or extend renderer/material/shader contract coverage if the root
      cause is in graphics-side config, colormap, or shader input wiring.
- [x] Run focused visualization extraction/sync tests, then the default
      CPU-supported correctness gate unless the slice is explicitly deferred.

## Docs
- [x] Update this task and regenerate `tasks/SESSION-BRIEF.md`.
- [x] Update rendering/runtime docs only if the scalar visualization contract or
      diagnostic schema changes.

## Acceptance criteria
- [x] Finite non-flat scalar properties no longer resolve to all-black output
      at the verified contract seam.
- [x] Runtime extraction and renderer sync expose enough counters/diagnostics to
      distinguish missing data from valid scalar visualization.
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
- Hiding the bug by forcing a uniform fallback color for scalar-field mode.
- Reintroducing live ECS/runtime ownership into graphics.
- Mixing runtime main-loop cleanup or unrelated UI reorganization into this
  bug fix.

## Maturity
- Target: `CPUContracted` for the runtime/graphics scalar contract in this
  slice. A Vulkan sandbox smoke is useful if available, but no `Operational`
  follow-up is owed unless the root cause proves backend-specific.

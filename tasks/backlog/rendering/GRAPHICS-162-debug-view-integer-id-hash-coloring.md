---
id: GRAPHICS-162
theme: B
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up; evidence is the diff and debug-view contract tests.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. Debug-view presentation of integer render targets only; no property, recipe-slot, locality or publication contract changes.
---
# GRAPHICS-162 — Debug view: integer ID hash coloring

## Goal
- Let the debug view display unsigned-integer targets (entity/primitive ID
  buffers) as distinct hash colors instead of raw or missing values.

## Context
- REVIEW-007 G01 deleted the unloaded compute variant. Reference:
  `assets/shaders/debug_view.comp @ 087e6e17b` (separate float/uint/depth
  inputs; integer IDs mapped through a hash color).
- The active path lacks this: `assets/shaders/debug_view.frag` defines
  `HashColor` but never calls it and has no unsigned-integer branch; depth is
  shown raw and float targets as color.
- Do not copy the old depth "linearization" formula unchecked; any depth
  change needs its own verified formula against the active projection.

## Acceptance criteria
- [ ] `DebugViewPushConstants`/resource classification identifies integer targets, and `debug_view.frag` samples them through an unsigned-integer path into `HashColor`.
- [ ] Float and depth targets render as before.
- [ ] Contract tests cover classification of an R32_UINT target; new `DefaultRecipeSurfaceGpuSmoke.DebugViewHashesEntityIdTarget` reads back distinct colors for distinct IDs on `ci-vulkan`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicShaderOutputs IntrinsicGraphicsContractCpuTests IntrinsicGraphicsRendererCpuUnitTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(GraphicsDebugViewContract|DebugViewPassContract|GraphicsDebugViewSystem)\.'
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicShaderOutputs IntrinsicGraphicsVulkanSmokeTests
# DebugViewHashesEntityIdTarget is the readback this task adds.
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^DefaultRecipeSurfaceGpuSmoke\.(DebugViewHashesEntityIdTarget|ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples)$'
```

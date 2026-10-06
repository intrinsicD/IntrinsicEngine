---
id: GRAPHICS-161
theme: B
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up; evidence is the diff, CPU contract tests and a gpu;vulkan readback smoke.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. Shader-quality change inside the existing forward line pass; no property binding, recipe slot, locality or publication contract changes.
---
# GRAPHICS-161 — Line anti-aliasing with extended segment ends

## Goal
- Give the active forward line path an analytic anti-aliased edge and
  half-width segment-end extension so thick polylines join without gaps.

## Context
- REVIEW-007 G01 deleted the unloaded root line shaders. Reference:
  `assets/shaders/line.vert @ 087e6e17b` (pixel-space quad expansion,
  extended ends, signed distance to the center line) and
  `assets/shaders/line.frag @ 087e6e17b` (smooth edge alpha from that distance
  and the push-constant width).
- The active path lacks this: `forward/line.vert` expands quads with variable
  width/color but builds the offset in NDC without end extension or a distance
  varying; `forward/line.frag` only resolves the visualization color, with no
  edge filter.
- Keep the active SceneTable/BDA ABI and per-element width binding
  (RUNTIME-315/GRAPHICS-158). Line picking quads are GRAPHICS-158's scope.

## Acceptance criteria
- [ ] `forward/line.vert` emits a pixel-space center-line distance and extends segment ends by half the width.
- [ ] `forward/line.frag` applies an analytic AA falloff; opaque/blend state stays correct for the line bucket.
- [ ] Width-1 lines and per-element widths still render; picking is unchanged.
- [ ] New `RuntimeSandboxAcceptanceGpuSmoke.LineEdgeAntialiasingAndJointCoverage` reads back a soft edge and no gap at a polyline joint on `ci-vulkan`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicShaderOutputs IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(GraphicsLinePointPassContracts|RendererFrameLifecycle)\.'
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicShaderOutputs IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
# LineEdgeAntialiasingAndJointCoverage is the readback this task adds.
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.(LineEdgeAntialiasingAndJointCoverage|ReferenceTriangleMeshConfiguredLineWidthAndPointDrawLanesPresent|ReferenceTriangleScalarFieldColormapResolvesOnLineAndPointLanes)$'
```

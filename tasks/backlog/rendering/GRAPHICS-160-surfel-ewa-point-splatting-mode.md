---
id: GRAPHICS-160
theme: B
depends_on: [RUNTIME-222]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up; evidence is the diff, CPU contract tests and a gpu;vulkan readback smoke.
contract_schema: 1
contracts: [geometry.property-coherence]
contract_review: The surfel mode renders from bound per-point normal and radius streams, so it is a renderer consumer of property-backed GPU data; no new canonical property, binding model or recipe slot is introduced.
maturity_target: Operational
---
# GRAPHICS-160 — Surfel/EWA point splatting mode

## Goal
- Add a selectable surfel/EWA splatting mode to the active retained point path:
  normal-oriented disks with a projected screen-space covariance and Gaussian
  footprint, falling back to a flat disk when the projection degenerates.

## Context
- REVIEW-007 G01 deleted the unloaded reference implementation. Read it at
  `assets/shaders/point_surfel.vert @ 087e6e17b` and
  `assets/shaders/point_surfel.frag @ 087e6e17b` (`git show 087e6e17b:<path>`):
  inverse-transpose normal matrix, projection Jacobian, covariance with a 3-sigma
  extent, Mahalanobis alpha with a 3-sigma cutoff, flat-disk fallback.
  Shared math still lives in `assets/shaders/common/point_splat.glsl`.
- The active path lacks this: `forward/point.vert` expands camera-facing pixel
  quads and sets a constant view normal (`vViewNormal = vec3(0, 0, 1)`);
  `forward/point.frag` has disc/sphere and a simplified surfel branch, no EWA.
- Do not copy the old Camera-UBO/push-constant ABI; use the SceneTable/BDA
  contract. EWA here is not 3D Gaussian splatting (no anisotropic 3D
  covariance, no view-dependent color).
- Radius units come from RUNTIME-222; per-point normal and size streams come
  from the RUNTIME-315 bindings proved by GRAPHICS-158.

## Acceptance criteria
- [ ] Point render config exposes the surfel/EWA mode through the existing validated recipe/config path; existing modes render unchanged.
- [ ] The vertex stage uses the bound per-point normal and model-space radius, with a flat-disk fallback for degenerate projections.
- [ ] Picking and depth stay consistent with the visible footprint.
- [ ] CPU pass contract covers mode selection; new `RuntimeSandboxAcceptanceGpuSmoke.SurfelPointFootprintFollowsBoundNormal` reads back an oriented, normal-dependent footprint on `ci-vulkan`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicShaderOutputs IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(GraphicsLinePointPassContracts|RendererFrameLifecycle)\.'
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicShaderOutputs IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
# SurfelPointFootprintFollowsBoundNormal is the readback this task adds.
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.(SurfelPointFootprintFollowsBoundNormal|ReferenceTriangleMeshConfiguredLineWidthAndPointDrawLanesPresent)$'
```

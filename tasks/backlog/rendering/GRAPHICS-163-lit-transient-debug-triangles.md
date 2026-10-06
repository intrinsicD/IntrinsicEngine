---
id: GRAPHICS-163
theme: B
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up; evidence is the diff, CPU contract tests and the transient-debug gpu;vulkan smoke.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog. Shading change inside the transient debug-triangle pass; no property binding, recipe slot, locality or publication contract changes.
---
# GRAPHICS-163 — Lit transient debug triangles with alpha pass-through

## Goal
- Shade transient debug triangles with diffuse lighting from the scene light
  packet and keep the caller's alpha, so translucent debug surfaces read as
  3D shapes.

## Context
- REVIEW-007 G01 deleted the unloaded reference. See
  `assets/shaders/debug_surface.vert @ 087e6e17b` (per-vertex normals) and
  `assets/shaders/debug_surface.frag @ 087e6e17b` (diffuse lighting, alpha
  passed through).
- The active path lacks this: `transient_debug_triangle.vert` fetches only
  position and color (no normal) and `transient_debug_triangle.frag` is unlit
  and forces alpha to 1.
- Reuse the active light packet and transient vertex BDA; do not restore the
  old Camera-UBO layout. Default lighting itself is RUNTIME-218's scope.

## Acceptance criteria
- [ ] Transient triangles carry or derive a normal and are lit from the active light packet.
- [ ] Alpha below 1 blends with correct blend state; opaque callers render unchanged.
- [ ] `TransientDebugSurfacePassContract` covers the new layout; new `TransientDebugSurfaceGpuSmoke.LitTrianglesShadeAndKeepAlpha` reads back shading variation and alpha on `ci-vulkan`.

## Verification
```bash
cmake --build --preset ci --target IntrinsicShaderOutputs IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^TransientDebugSurfacePassContract\.'
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicShaderOutputs IntrinsicGraphicsVulkanSmokeTests
# LitTrianglesShadeAndKeepAlpha is the readback this task adds.
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^TransientDebugSurfaceGpuSmoke\.(LitTrianglesShadeAndKeepAlpha|MixedLanesReadBackExpectedSampleColors|WorldSpacePacketsFollowTheCameraAndPointsKeepTheirRadius)$'
```

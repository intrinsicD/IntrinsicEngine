---
id: GRAPHICS-115
theme: B
depends_on:
  - GRAPHICS-104
maturity_target: Operational
---
# GRAPHICS-115 — Object-space normal GPU dilation pass

## Goal
- Add the GPU image-space dilation path for object-space normal texture bakes so
  requested padding can fill gutter texels on GPU instead of failing closed.

## Non-goals
- No Engine/import scheduling or material-binding swap; `RUNTIME-129` owns the
  runtime orchestration.
- No CPU dilation fallback.
- No tangent-space normal-map baking or MikkTSpace tangent generation.

## Context
- Owning subsystem/layer: `graphics` and `graphics/rhi`.
- `GRAPHICS-104` retired the zero-padding raster-bake and cache-residency
  contract at `CPUContracted`. It intentionally reports
  `DilationUnavailable` for padded requests because the current RHI/Vulkan
  bindless heap exposes sampled combined-image slots, not a storage-image write
  surface.
- This task must choose and implement a backend-neutral GPU dilation mechanism:
  either extend RHI/Vulkan descriptor support for storage-image writes, or use a
  graphics-owned ping-pong render/fragment path with explicit temporary texture
  ownership. The choice must preserve the "no `Vk*` through RHI/renderer APIs"
  invariant.

## Required changes
- [ ] Add a graphics-owned dilation plan/recording API for object-space normal
      bakes that fills requested padding texels on GPU.
- [ ] Allocate and own any temporary texture/resource leases needed by the
      dilation path without leaking backend-native handles.
- [ ] Update `BuildObjectSpaceNormalTextureBakePlan(...)` so padded requests
      become submittable only when the dilation resources/pipeline are available.
- [ ] Preserve the zero-padding fast path and deterministic fail-closed status
      when dilation resources are unavailable.

## Tests
- [ ] CPU/null contract tests for padded-plan resource requirements, command
      shape, clamped diagnostics, and fail-closed unavailable behavior.
- [ ] Shader-source or command-recording contract tests that prove dilation
      reads covered texels and fills neighboring uncovered alpha-zero texels.
- [ ] Opt-in `gpu;vulkan` smoke that submits a small padded triangle bake and
      verifies selected gutter texels.

## Docs
- [ ] Update `src/graphics/renderer/README.md` with the selected dilation
      mechanism, resource ownership, and fallback behavior.
- [ ] Update `src/graphics/assets/README.md` if the produced texture usage flags
      or cache ownership contract changes.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces
      change.

## Acceptance criteria
- [ ] Requested padding no longer reports `DilationUnavailable` when the graphics
      dilation resources are available.
- [ ] Dilation fills gutter texels on GPU and keeps covered texels unchanged.
- [ ] Non-operational or missing-resource paths fail closed without CPU fallback.
- [ ] Layering holds: no live ECS/runtime/AssetService imports in graphics and no
      `Vk*` types through public APIs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalTextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ObjectSpaceNormalTextureBake' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding CPU dilation fallback.
- Passing `Vk*` types through RHI, renderer, runtime, or cache public APIs.
- Mixing runtime import scheduling with graphics dilation implementation.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for CPU/null
  command-shape and fail-closed contracts.

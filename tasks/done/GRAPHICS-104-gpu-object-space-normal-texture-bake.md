---
id: GRAPHICS-104
theme: B
depends_on:
  - GEOM-025
  - GEOM-026
  - GRAPHICS-015
  - GRAPHICS-088
  - GRAPHICS-098
  - RUNTIME-119
maturity_target: CPUContracted
completed_on: 2026-07-02
---
# GRAPHICS-104 — GPU Object-Space Normal Texture Bake

## Goal
- Establish the graphics/runtime CPU-contract seam for GPU-produced object-space
  normal texture bakes: graphics-owned raster bake plans/recording, GPU asset
  cache residency, material/shader metadata, and runtime queue/submission/binding
  contracts.

## Non-goals
- No storage-image or ping-pong GPU dilation pass in this retired slice;
  `GRAPHICS-115` owns the `Operational` dilation path.
- No Engine/import render-thread scheduling or replacement of CPU import
  generated-normal bakes in this retired slice; `RUNTIME-129` owns that runtime
  orchestration.
- No tangent-space normal baking, MikkTSpace tangent generation, or CPU fallback
  for GPU bake failure.

## Context
- Status: completed 2026-07-02 at `CPUContracted`.
- Owning subsystem/layer: `graphics` owns the RHI bake API, shaders, and
  `GpuAssetCache` GPU-produced texture residency; `runtime` owns stale keys,
  generated `AssetId` selection, queue submission contracts, and data-only
  material binding handoff.
- The implemented path records atlas-UV raster bakes for zero-padding requests,
  packages generated texture `AssetId`s, keeps cache entries pending until a
  ready frame is observed, and lets runtime bind only cache-ready generated
  textures through `RenderExtractionCache`.
- The original active task bundled later operational work. That work is now
  explicit follow-up scope: `GRAPHICS-115` for GPU dilation and `RUNTIME-129`
  for import/render-thread scheduling and end-to-end material swaps.

## Required changes
- [x] Add graphics-owned object-space normal bake DTOs, validation helpers,
      stale completion keys, GPU-produced texture plan packaging, pipeline
      descriptor builder, and raster command recorder.
- [x] Add bake shaders that rasterize atlas UV triangles into an `RGBA8_UNORM`
      target, clear uncovered texels to encoded `+Z` alpha `0`, interpolate and
      normalize object-space normals, and write covered alpha `1`.
- [x] Extend `GpuAssetCache` with a GPU-produced pending texture path keyed by
      generated `AssetId`, including frame-number readiness promotion.
- [x] Add object-space normal material metadata and update forward/deferred
      shader contracts to consume object-space normal maps only behind the
      explicit material flag and a valid normal texture id.
- [x] Mark generated-normal material bindings as `ObjectSpaceNormal` in the
      existing CPU compatibility paths so shader behavior is explicit.
- [x] Add runtime queue, submission, and binding helpers for generated texture
      `AssetId` selection, content-key reuse, stale-key matching, non-operational
      no-op behavior, cache allocation, ready-frame promotion, and material
      binding handoff.
- [x] Move unimplemented operational requirements into follow-up tasks:
      `GRAPHICS-115` for GPU dilation and `RUNTIME-129` for Engine/import
      scheduling.

## Tests
- [x] Add CPU/null contract tests for bake validation, extent/default/clamp
      policy, fail-closed sampling, generated asset key selection, stale
      completion discard, non-operational no-op behavior, and material
      flag/binding behavior.
- [x] Add `GpuAssetCache` tests for GPU-produced pending registration,
      readiness promotion, fallback/default view behavior before readiness, and
      retirement/reload interactions.
- [x] Add command-recording contract coverage for target extent, clear coverage,
      push constants, indexed draw, and invalid-resource rejection.
- [x] Add shader/material source-contract tests for object-space normal map
      gating in forward and deferred/G-buffer shaders.
- [x] Add deterministic bake math/orientation tests and an opt-in `gpu;vulkan`
      smoke for direct raster-bake selected texels.
- [x] Leave engine-level import scheduling, cache-ready material swaps from real
      import jobs, and dilation smoke coverage to `RUNTIME-129` / `GRAPHICS-115`.

## Docs
- [x] Update `src/graphics/assets/README.md` for GPU-produced generated texture
      residency, cache ownership, readiness, and material binding semantics.
- [x] Update `src/graphics/renderer/README.md` for the object-space normal bake
      pass, shader consumption flag, forward/deferred behavior, and current
      zero-padding/non-operational behavior.
- [x] Update `src/runtime/README.md` for queue scheduling metadata, stale-key
      semantics, non-operational no-op behavior, and deferred import replacement.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module-surface
      changes.
- [x] Open `GRAPHICS-115` and retain `RUNTIME-129` for the remaining operational
      work.

## Acceptance criteria
- [x] V1 reserves `TangentSpaceNormal` API space but supports only
      `ObjectSpaceNormal`.
- [x] The graphics bake API consumes resolved render-facing geometry buffers,
      not ECS, runtime, or live asset-service state.
- [x] Zero-padding bake plans produce GPU-resident generated textures addressed
      by `AssetId`, and cache views become material-ready only after frame
      readiness promotion.
- [x] Forward and deferred surface shading honor object-space normal textures
      only under the explicit material flag and otherwise preserve vertex-normal
      behavior.
- [x] Runtime queue/submission/binding contracts reject stale completions before
      material mutation and no-op without CPU fallback on non-operational
      backends.
- [x] Requested padding currently fails closed with `DilationUnavailable`;
      `GRAPHICS-115` owns the GPU dilation path before default padded bakes can
      claim `Operational`.
- [x] Import/enrichment replacement and end-to-end material swaps are owned by
      `RUNTIME-129`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

Completed 2026-07-02 at `CPUContracted`.
Commit reference: this commit.

Latest recorded slice evidence before retirement included focused
`ObjectSpaceNormalTextureBake`, `ObjectSpaceNormalBakeQueue`,
`ObjectSpaceNormalBakeSubmission`, `GpuAssetCache`, shader contract, default CPU
gate, and opt-in Vulkan direct-raster-bake smoke runs. This retirement commit
re-runs the task/documentation structural checks.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding live ECS, runtime, or asset-service knowledge to graphics-owned bake
  modules.
- Passing `Vk*` types through RHI, renderer, runtime, or cache public APIs.
- Treating ordinary glTF tangent-space normal textures as object-space normal
  maps.
- Claiming GPU dilation or import-time operational scheduling before
  `GRAPHICS-115` / `RUNTIME-129` land.

## Maturity
- Target reached: `CPUContracted`.
- `Operational` dilation is owned by `GRAPHICS-115`.
- `Operational` import/render-thread scheduling and generated-normal replacement
  is owned by `RUNTIME-129`.

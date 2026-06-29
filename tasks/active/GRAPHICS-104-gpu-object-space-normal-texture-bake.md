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
maturity_target: Operational
---
# GRAPHICS-104 — GPU Object-Space Normal Texture Bake

## Goal
- Replace the generated object-space normal-map bake path with an asynchronous GPU raster bake that consumes resolved mesh UVs and normals, produces a GPU-resident generated texture, and binds it through the existing material texture path once the GPU work is complete.

## Non-goals
- No tangent-space normal-map baking or MikkTSpace tangent generation.
- No world-space normal-map persistence.
- No generic GPU port of scalar, label, vector, face-domain, or selected-mesh attribute baking.
- No CPU fallback for GPU bake failure or non-operational graphics backends.
- No mandatory CPU texture payload/readback/export for generated GPU bakes.
- No support for graph, point-cloud, line, or non-triangle domains in this task.
- No exhaustive UV-overlap detector in the default bake path.

## Context
- Owned by `graphics` for the RHI pass, shaders, GPU texture production, and cache residency; owned by `runtime` for entity/job orchestration, generated `AssetId` selection, stale-generation checks, and material binding.
- The current CPU bake path in `Extrinsic.Runtime.MeshAttributeTextureBake` and direct mesh import post-process is too slow and is not trusted as the correctness source for normal interpolation/orientation.
- V1 supports only `ObjectSpaceNormal`; API enum space should reserve `TangentSpaceNormal` for a future task.
- Input UVs must already be atlas-style finite UVs in `[0, 1]` within a small epsilon and must be non-overlapping by contract.
- Input object-space normals must already exist in the resolved render mesh; normal recomputation remains an upstream geometry/runtime job.
- The bake output is `RGBA8_UNORM` linear data: RGB stores `normal * 0.5 + 0.5`, alpha stores coverage/debug.
- Bake resolution comes from explicit request, then generated atlas metadata when available, then default `512x512`; valid requests clamp to a documented min/max such as `16..4096`.
- GPU output should be addressed by a generated `AssetId` so existing `MaterialTextureAssetBindings.Normal`, `MaterialSystem`, and `GpuAssetCache` resolution stay the material path.
- The rasterizer target extent is the bake texture extent; the bake vertex shader maps `uv` into clip space so hardware interpolation writes the matching texel grid.
- Imported/generated meshes may render with vertex normals for a few frames while the async GPU bake finishes.

## Required changes
- [x] Refine and keep current the slice checklist as implementation discoveries land, including any deliberate deferrals with follow-up ownership.
- [x] Add a graphics-owned object-space normal bake RHI command recorder that consumes resolved texcoord/normal BDA buffers, index buffer, target extent, output texture, and pipeline handle without importing ECS, runtime, or live `AssetService`.
- [x] Add a graphics-owned object-space normal bake API/subsystem that accepts resolved triangle mesh buffer/geometry-record data, bake extent, padding, generated asset id/key, and stale-generation metadata without importing ECS, runtime, or live `AssetService`.
- [x] Extend `GpuAssetCache` with a GPU-produced pending texture path keyed by `AssetId`, allocating cache-owned textures with `Sampled | ColorTarget` and reserving `Storage` usage for the future dilation path.
- [x] Track GPU completion for produced textures and promote cache entries to `Ready` only after the submitted graphics work has completed.
- [x] Add bake shaders that rasterize atlas UV triangles into an `RGBA8_UNORM` target, interpolate and normalize object-space normals, write alpha coverage, and use a shader-sampling-equivalent orientation.
- [ ] Add GPU dilation as an image-space compute post-pass with request padding defaulting to 4 texels and clamped diagnostics; if descriptor plumbing prevents dilation in the first implementation, fail closed or report padding unavailable rather than CPU-dilating.
- [x] Add material metadata for object-space normal textures, including an API enum reserving tangent-space support and a shader-visible flag such as `HasObjectSpaceNormalMap`.
- [x] Update forward and deferred/G-buffer shaders to sample object-space normal maps only when the material flag is set and `NormalID` is valid, decode RGB to object-space normal, transform through the model normal matrix, normalize, and otherwise preserve vertex-normal behavior.
- [x] Mark current generated-normal material bindings as `ObjectSpaceNormal` in direct import, model-scene handoff, and progressive presentation extraction; full GPU job scheduling remains owned by the runtime orchestration item below.
- [x] Add a runtime queue contract that schedules explicit GPU object-space normal bake requests, records stale keys for entity, geometry/UV/normal generation, resolution, padding, and normal-map type, no-ops on non-operational graphics backends without CPU fallback, and rejects stale completions before material mutation.
- [x] Add a runtime cache-ready binding helper that consumes queue completions only after `GpuAssetCache` exposes a ready generated texture view, rejects stale completions before material mutation, and installs data-only `ObjectSpaceNormal` material bindings through `RenderExtractionCache`.
- [x] Add a runtime GPU submission helper that validates queued stale keys against graphics bake plans, registers cache-owned GPU-produced textures, returns render-thread record descriptors, and promotes cache readiness from submitted-frame completion values without completing the queue early.
- [ ] Wire runtime orchestration into import/render-thread scheduling so queued GPU object-space normal bake jobs are submitted and completed against real cache readiness.
- [ ] Route import/enrichment generated-normal use cases through the new GPU job API once the end-to-end GPU path is implemented, retaining the CPU path only as legacy compatibility until replacement is complete.
- [x] Select generated texture `AssetId`s through the runtime queue by resolved geometry/UV/normal content key where available, with entity-scoped generated `AssetId` fallback when no stable geometry key exists.
- [ ] Keep completed bakes reusable end-to-end after cache readiness, including material binding updates for content-key hits.
- [ ] Fail closed with deterministic diagnostics for missing/non-finite UVs, missing/non-finite normals, non-atlas UVs, degenerate UV triangles, zero coverage, non-triangle meshes, non-operational graphics backends, and stale completion.

## Tests
- [x] Add CPU/null contract tests for object-space bake request validation, resolution/default/clamp policy, fail-closed sampling, and material flag/binding behavior.
- [x] Add CPU/null contract tests for generated asset key selection, non-operational no-op, and runtime stale completion discard in the runtime queue contract.
- [x] Add CPU/null contract tests for cache-ready object-space normal bake binding, pending-cache no-op behavior, content-key reuse binding, and stale-completion rejection before material mutation.
- [x] Add CPU/null contract tests for GPU submission preparation, stale-plan rejection before cache allocation, ready-frame promotion, and binding handoff after cache readiness.
- [ ] Add CPU/null contract tests for end-to-end material binding transition once runtime GPU orchestration lands.
- [x] Add `GpuAssetCache` tests for GPU-produced pending texture registration, readiness promotion, fallback/default view behavior before readiness, and retirement/reload interactions.
- [x] Add command-recording contract coverage for the GPU bake pass shape, target extent, clear coverage, push constants, and indexed draw.
- [x] Add shader-source contract checks for modified surface/G-buffer shaders.
- [x] Add shader/material contract tests that verify object-space normal maps are gated by explicit material metadata and not inferred solely from `NormalID`.
- [x] Add graphics API contract tests for generated texture `AssetId` packaging, stale completion keys, `GpuProducedTextureRequest` output descriptors, and command-record template instantiation.
- [x] Add a focused asymmetric UV/orientation test at the plan or shader-contract level so the baked texture samples the same surface point the mesh UV refers to.
- [x] Add a deterministic point-sample bake correctness test with known object-space normals at selected UV/sample points; prefer GPU texture readback when available, otherwise test the shared bake math/plan used by the GPU path.
- [x] Add an opt-in `gpu;vulkan` smoke that submits a small triangle bake on a Vulkan-capable host and verifies selected output texels.
- [x] Add texture readback pixel verification only if existing readback plumbing supports it without expanding this task's scope.

## Docs
- [x] Update `src/graphics/assets/README.md` for GPU-produced generated texture residency, cache ownership, readiness, and material binding semantics.
- [x] Update `src/graphics/renderer/README.md` for the object-space normal bake pass, shader consumption flag, forward/deferred behavior, dilation/mip policy, and non-operational backend behavior.
- [x] Update `src/runtime/README.md` for current object-space generated-normal binding metadata, runtime queue scheduling/stale-key semantics, non-operational no-op behavior, and the deferred replacement of the direct CPU generated-normal post-process path.
- [x] Update architecture docs if the final API adds or changes a public module surface or dependency boundary.
- [x] Regenerate `docs/api/generated/module_inventory.md` if any `.cppm` module surfaces are added or changed.

## Acceptance criteria
- [x] V1 supports only `ObjectSpaceNormal` while reserving API space for future `TangentSpaceNormal`.
- [ ] The GPU bake consumes resolved render geometry data, not a separate CPU-only mesh upload path.
- [ ] The output texture is GPU-resident, generated `AssetId` addressed, and material-ready only after GPU completion.
- [x] The bake pass uses rasterization over atlas UVs at the exact target texture extent.
- [x] Covered texels encode object-space normals with alpha 1; uncovered texels clear deterministically to encoded `+Z` with alpha 0 in the CPU contract and opt-in Vulkan smoke.
- [ ] Dilation fills requested gutter texels on GPU when enabled.
- [ ] Default output is `RGBA8_UNORM`, linear, one mip unless a correct dilation-aware mip path already exists.
- [ ] Non-operational graphics backends do not run a CPU fallback and keep previous/default shading behavior with diagnostics.
- [x] Cache-ready queue completions can install object-space normal material bindings without consuming pending cache entries or applying stale completions.
- [x] Runtime GPU submission preparation registers cache-owned produced textures and defers queue completion until the cache reaches a submitted ready frame.
- [x] Forward and deferred surface shading both honor object-space normal textures under the explicit material flag.
- [ ] Existing generated normal-map import/enrichment behavior switches to the GPU path only after the GPU bake, cache registration, shader consumption, and runtime stale-key flow are implemented end to end.

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

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated renderer, asset, runtime, UI, or geometry features.
- Adding live ECS, runtime, or asset-service knowledge to graphics-owned modules.
- Passing `Vk*` types through RHI, renderer, runtime, or cache public APIs.
- Deleting the CPU texture bake path before the GPU replacement is complete and verified.
- Treating ordinary glTF/material tangent-space normal textures as object-space normal maps.
- Silently accepting repeating/tiled/non-atlas UVs for this bake type.
- Blocking asset import on GPU bake completion.

## Slice plan
- **Slice A (implemented).** Add request/status DTOs, validation helpers, shader-equivalent point sampling, and `GpuAssetCache` pending GPU-produced texture contracts. Close at `CPUContracted`.
- **Slice B (implemented).** Add graphics-owned raster bake command recorder and shaders, plus command-recording contract coverage. Close at `CPUContracted`.
- **Slice C (implemented).** Add object-space normal-map material metadata and forward/deferred shader consumption. Close at `CPUContracted`.
- **Slice D (partial).** Generated-normal bindings now carry `ObjectSpaceNormal` provenance for direct import, model-scene handoff, and progressive presentation extraction. The runtime queue contract now selects generated texture assets by content key, records stale keys, no-ops without CPU fallback on non-operational graphics backends, and discards stale completions before material mutation. `Runtime.ObjectSpaceNormalBakeBinding` now bridges cache-ready completions into data-only `RenderExtractionCache` material bindings without consuming pending cache entries or applying stale completions. `Runtime.ObjectSpaceNormalBakeSubmission` now validates queued stale keys against graphics bake plans, registers cache-owned GPU-produced textures, returns record descriptors for render-thread command recording, and marks ready frames without completing the queue early. Engine import scheduling, render-thread command submission, GPU command completion feed integration, and import/enrichment replacement remain open. Close target remains `CPUContracted`.
- **Slice E (partial).** Add Vulkan operational smoke with selected-texel readback. Still requires cache-ready promotion proof from a submitted bake and switching generated object-space normal import/enrichment to the GPU path before closing `Operational`.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for CPU/null validation.
- This task closes `Scaffolded → Operational`; no separate `Operational` follow-up is owed unless Slice E is split into a new task before retirement.

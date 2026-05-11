# GRAPHICS-032B — `Pass.Surface.MinimalDebug` CPU-mock command body

## Goal
- Implement the property-based CPU-mock command-recording body for `Pass.Surface.MinimalDebug`: bind the GRAPHICS-031 default-debug-surface pipeline, bind the `GpuWorld` index buffer, push scene-table BDA push constants, and `DrawIndexedIndirectCount` against the `SurfaceOpaque` cull bucket. The pass writes `SceneColorHDR` + `SceneDepth` and consumes `MaterialBuffer` SSBO at `set = 3, binding = 0`.

## Non-goals
- No `Pass.Present.MinimalDebug` body (that is `GRAPHICS-032C`).
- No `gpu;vulkan` smoke (that is `GRAPHICS-032D`).
- No new diagnostics counters beyond those declared by `GRAPHICS-032A`.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](../../done/GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-B in the parent's Required changes.
- Upstream gates: `GRAPHICS-032A` (recipe must exist), `GRAPHICS-031A` (slot-0 pipeline), `GRAPHICS-030B` (renderable + geometry binding so the cull bucket has anything to draw).
- The pass body is symmetric to `Pass.Forward.Surface.cpp:14` — same `BindPipeline` / `BindIndexBuffer` / `PushConstants` / `DrawIndexedIndirectCount` shape, but it uses the slot-0 default-debug pipeline.

## Required changes
- [ ] Add `class MinimalDebugSurfacePass` (header in `Passes/Pass.Surface.MinimalDebug.cppm`, body in `Passes/Pass.Surface.MinimalDebug.cpp`) with `SetPipeline(PipelineHandle)` + `Execute(cmd, camera, gpuWorld, culling, frameIndex)` interface mirroring `ForwardSurfacePass`.
- [ ] Have `NullRenderer` own a `MinimalDebugSurfacePass` instance, set its pipeline from the GRAPHICS-031A slot-0 lease (republished by `RebuildOperationalResources()`).
- [ ] Extend the renderer's executor lambda (`Graphics.Renderer.cpp` per-pass dispatch, around the existing `"CullingPass"` / `"DepthPrepass"` branches) to add a `"Pass.Surface.MinimalDebug"` branch routing to a new `RecordMinimalDebugSurfacePass(...)` helper that:
  - returns `SkippedNonOperational` when device is non-operational,
  - returns `SkippedUnavailable` when slot-0 pipeline lease or the `SurfaceOpaque` cull bucket is missing (and increments `MinimalRecipeMissingPrerequisiteCount`),
  - otherwise calls `MinimalDebugSurfacePass::Execute(...)` and increments `MinimalSurfacePassExecutions`.

## Tests
- [ ] `contract;graphics` test: with the minimal recipe + GRAPHICS-031A slot-0 pipeline + an extracted procedural triangle, the recorded command stream contains in order: `BindPipeline(default-debug-surface)`, `BindIndexBuffer(gpuWorld.GetManagedIndexBuffer())`, `PushConstants(GpuScenePushConstants{ DrawBucket = SurfaceOpaque, ... })`, `DrawIndexedIndirectCount(...)`.
- [ ] `contract;graphics` test: missing pipeline → `MinimalRecipeMissingPrerequisiteCount` increments; missing cull bucket → counter increments; missing residency → counter increments.
- [ ] `contract;graphics` test: `MinimalSurfacePassExecutions` increments by 1 per successful record.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to list the new `Pass.Surface.MinimalDebug` module.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] The minimal-recipe Surface pass routes to the new helper instead of the executor's "everything else" soft-skip branch.
- [ ] All counter increments match the recorded test expectations.
- [ ] No regression in the default-recipe `CullingPass`/`DepthPrepass` routes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Implementing `Pass.Present.MinimalDebug` (reserved for `GRAPHICS-032C`).
- Adding new pipelines beyond reuse of the slot-0 lease.
- Mutating runtime ECS state.
- Adding a `gpu;vulkan` test.

## Next verification step
- Land the pass class + executor route, exercise the property-based contract tests above.

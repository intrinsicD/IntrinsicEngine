# GRAPHICS-032B — `Pass.Surface.MinimalDebug` CPU-mock command body

## Goal
- Implement the property-based CPU-mock command-recording body for `Pass.Surface.MinimalDebug`: bind the GRAPHICS-031 default-debug-surface pipeline, bind the `GpuWorld` index buffer, push scene-table BDA push constants, and `DrawIndexedIndirectCount` against the `SurfaceOpaque` cull bucket. The pass writes `SceneColorHDR` + `SceneDepth` and consumes `MaterialBuffer` SSBO at `set = 3, binding = 0`.

> **Scaffold notice.** `Pass.Surface.MinimalDebug` is removed by [`GRAPHICS-081`](../backlog/rendering/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) once `Pass.Forward.Surface` is operationally wired (`GRAPHICS-070`) and the rest of the default recipe lands. The canonical surface lane lives in `Pass.Forward.Surface`; this pass is only the bootstrap derisker.

## Non-goals
- No `Pass.Present.MinimalDebug` body (that is `GRAPHICS-032C`).
- No `gpu;vulkan` smoke (that is `GRAPHICS-032D`).
- No new diagnostics counters beyond those declared by `GRAPHICS-032A`.

## Context
- Status: done.
- Owner/agent: Claude on branch `claude/setup-agentic-workflow-uLLKn`.
- Owner/layer: `graphics/renderer`.
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](../done/GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-B in the parent's Required changes.
- Upstream gates: `GRAPHICS-032A` (recipe must exist), `GRAPHICS-031A` (slot-0 pipeline), `GRAPHICS-030B` (renderable + geometry binding so the cull bucket has anything to draw).
- The pass body is symmetric to `Pass.Forward.Surface.cpp:14` — same `BindPipeline` / `BindIndexBuffer` / `PushConstants` / `DrawIndexedIndirectCount` shape, but it uses the slot-0 default-debug pipeline. The current implementation actually mirrors the simpler `DepthPrepassPass` shape (no `ForwardSystem` dependency) because the minimal recipe does not own any forward-system state; the recorded command stream is identical aside from the pipeline handle.

## Required changes
- [x] Add `class MinimalDebugSurfacePass` (header in `Passes/Pass.Surface.MinimalDebug.cppm`, body in `Passes/Pass.Surface.MinimalDebug.cpp`) with `SetPipeline(PipelineHandle)` + `Execute(cmd, camera, gpuWorld, culling, frameIndex)` interface mirroring `ForwardSurfacePass`. Exposes `GetPipeline()` for the renderer's residency gate.
- [x] Have `NullRenderer` own a `MinimalDebugSurfacePass m_MinimalDebugSurfacePass` instance and set its pipeline from the GRAPHICS-031A slot-0 lease at the same site that creates `m_DefaultDebugSurfacePipelineLease` (republished byte-identically by `RebuildOperationalResources()`). The pipeline is reset to invalid on `Shutdown()` and on every entry into `InitializeOperationalPassResources()`.
- [x] Extend the renderer's executor lambda (`Graphics.Renderer.cpp` per-pass dispatch, around the existing `"CullingPass"` / `"DepthPrepass"` branches) with a `"Pass.Surface.MinimalDebug"` branch (matched against `kMinimalDebugSurfacePassName`) routing to a new `RecordMinimalDebugSurfacePass(...)` helper that:
  - returns `SkippedNonOperational` when device is non-operational,
  - returns `SkippedUnavailable` when the slot-0 pipeline lease, the `SurfaceOpaque` cull bucket, or the `GpuWorld` scene-table residency is missing (and increments `MinimalRecipeMissingPrerequisiteCount`),
  - otherwise calls `MinimalDebugSurfacePass::Execute(...)` and increments `MinimalSurfacePassExecutions`.
- [x] Convert the recipe-build-time assignment of `MinimalRecipeMissingPrerequisiteCount` to `+=` so build-time and record-time gaps accumulate into the same per-frame counter, preserving the existing per-frame reset cadence (`m_LastRenderGraphStats = {}` at `ResetFrameState`/`ExecuteFrame` entry).

## Tests
- [x] `contract;graphics` test: pass-class direct invocation with the slot-0 pipeline produces ordered events `BindPipeline → BindIndexBuffer → PushConstants → DrawIndexedIndirectCount`, with push constants carrying `SceneTableBDA`, `FrameIndex`, and `DrawBucket = SurfaceOpaque`, and the indirect args/count buffers matching the `SurfaceOpaque` bucket; the empty-pipeline case records no events. (`Test.MinimalDebugSurfacePass.cpp::ExecuteRecordsSurfaceOpaqueIndirectDrawInOrder`)
- [x] `contract;graphics` test: renderer with `FrameRecipeKind::MinimalDebug` increments `MinimalSurfacePassExecutions` by 1 per successful record and routes the pass to the `Recorded` status in `RenderGraphCommandPassStats`. (`RendererRoutesAndIncrementsExecutionsCounter`)
- [x] `contract;graphics` test: forcing the slot-0 pipeline lease to fail (`FailPipelineCreateCall = 3`) routes the pass to `SkippedUnavailable` and bumps `MinimalRecipeMissingPrerequisiteCount`. (`MissingSlotZeroPipelineLeaseSkipsUnavailableAndIncrementsCounter`)
- [x] `contract;graphics` test: forcing the culling-shader pipeline to fail (`FailPipelineCreateCall = 1`) leaves the `SurfaceOpaque` bucket invalid, routes the pass to `SkippedUnavailable`, and bumps `MinimalRecipeMissingPrerequisiteCount`. (`MissingCullingBucketSkipsUnavailableAndIncrementsCounter`)
- [x] `contract;graphics` test: flipping the device to non-operational between `Initialize` and `ExecuteFrame` routes the pass to `SkippedNonOperational` and leaves `MinimalSurfacePassExecutions` at zero. (`NonOperationalDeviceSkipsNonOperational`)
- Out of scope for this task: no `gpu`/`vulkan` test in this slice (covered by GRAPHICS-032D).

## Docs
- [x] Update `src/graphics/renderer/README.md` to list the new `Pass.Surface.MinimalDebug` module, point at the record site, and describe the additive (build + record-time) cadence of `MinimalRecipeMissingPrerequisiteCount`.
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module (total: 428 modules; `graphics/renderer` count 50 → 51).

## Acceptance criteria
- [x] The minimal-recipe Surface pass routes to `RecordMinimalDebugSurfacePass` instead of the executor's "everything else" soft-skip branch.
- [x] All counter increments match the recorded test expectations.
- [x] No regression in the default-recipe `CullingPass`/`DepthPrepass` routes (the new branch matches on `kMinimalDebugSurfacePassName` only).

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

## Completion
- Completed: 2026-05-13.
- Commit reference: `7fff8ca` ("GRAPHICS-032B: wire MinimalDebugSurface CPU-mock pass body") via PR #821 from `claude/setup-agentic-workflow-uLLKn`, merged to `main` at 2026-05-13T17:34:28Z.
- Verification:
  - Project CI ran on PR #821 (`ci` preset, clang-20 toolchain) and passed before merge to `main`.
  - Authoring session ran the structural checks locally; the focused `cmake --preset ci` / `ctest -L contract` gate ran in the PR's CI environment because the authoring container shipped clang-18 only.
  - `docs/api/generated/module_inventory.md` was regenerated as part of the PR (graphics/renderer module count 50 → 51; total 427 → 428).

# GRAPHICS-032C — `Pass.Present.MinimalDebug` body and end-to-end CPU acceptance test

## Goal
- Implement the property-based CPU-mock command-recording body for `Pass.Present.MinimalDebug` (bind present pipeline, `Draw(3, 1, 0, 0)`) and add the end-to-end CPU acceptance test that runs `recipe → BeginFrame → SubmitRuntimeSnapshots → ExtractRenderWorld → PrepareFrame → ExecuteFrame → EndFrame` and asserts the command stream order, recorded barriers, and counter values for one frame of `MinimalDebugSurface` with one procedural triangle.

> **Scaffold notice.** `Pass.Present.MinimalDebug` and the end-to-end CPU acceptance driver authored here were removed by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) after the canonical `Pass.Present` became operationally wired (`GRAPHICS-076`) and the default-recipe CPU acceptance covered the same shape. The reusable helper intent moved to default-recipe coverage; the minimal-recipe driver invocation is deleted.

## Retirement note

- Retired by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) on 2026-06-02. The bootstrap present pass class, renderer route, counters, tests, and module-inventory rows authored by this task are deleted.

## Non-goals
- No `gpu;vulkan` smoke (that is `GRAPHICS-032D`).
- No new diagnostics counters beyond those declared by `GRAPHICS-032A`.
- No mutation of the default recipe's skip/no-op statuses.

## Context
- Status: done.
- Owner/agent: Claude on branch `claude/inspect-engine-state-4lqQS` (implementation), `claude/inspect-engine-state-ltEmM` (retirement).
- Owner/layer: `graphics/renderer`.
- Planning parent: [`tasks/archive/GRAPHICS-032-minimal-surface-present-command-path.md`](GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-C in the parent's Required changes.
- Upstream gates: `GRAPHICS-032B` (done), `GRAPHICS-031A` (done) (the present pipeline can reuse the slot-0 fullscreen-triangle path or be a dedicated minimal-present pipeline; the implementer chooses, the contract is the recorded `Draw(3,1,0,0)` form).
- Note: `Pass.Present.MinimalDebug` and the existing `Pass.Present` (`Pass.Present.cpp:14`) share the same fullscreen-triangle command shape; the minimal variant exists so the minimal recipe stays self-contained.

## Required changes
- [x] Add `class MinimalDebugPresentPass` (header in `Passes/Pass.Present.MinimalDebug.cppm`, body in `Passes/Pass.Present.MinimalDebug.cpp`) with `SetPipeline(PipelineHandle)` + `Execute(cmd)`. Body matches `PresentPass::Execute`: guard pipeline validity, `BindPipeline` + `Draw(3, 1, 0, 0)`. Adds a `GetPipeline()` accessor to mirror `MinimalDebugSurfacePass` for the renderer's residency gate.
- [x] Have `NullRenderer` own a `MinimalDebugPresentPass m_MinimalDebugPresentPass` instance; reuse the GRAPHICS-031A slot-0 default-debug-surface pipeline lease (the contract is the recorded `Draw(3,1,0,0)` form, not pipeline-state details). The pass pipeline is reset to invalid on `Shutdown()` and at every entry into `InitializeOperationalPassResources()` and republished from the same lease on rebuild so the recorded command stream stays byte-identical across operational rebuilds.
- [x] Extend the renderer executor lambda (`Graphics.Renderer.cpp` per-pass dispatch, after the existing `kMinimalDebugSurfacePassName` branch) with a `"Pass.Present.MinimalDebug"` branch (matched against `kMinimalDebugPresentPassName`) routing to a new `RecordMinimalDebugPresentPass(...)` helper that:
  - returns `SkippedNonOperational` when device is non-operational,
  - returns `SkippedUnavailable` when the slot-0 pipeline lease is missing (and increments `MinimalRecipeMissingPrerequisiteCount`),
  - otherwise calls `MinimalDebugPresentPass::Execute(...)` and increments `MinimalPresentPassExecutions`.
- [x] End-to-end CPU acceptance test driver (renderer-integrated, on the operational `Tests::MockDevice`) drives one frame of the minimal-debug-surface recipe via `BeginFrame → ExtractRenderWorld → PrepareFrame → ExecuteFrame` and asserts:
  - `MinimalSurfacePassExecutions == 1`,
  - `MinimalPresentPassExecutions == 1`,
  - `MinimalRecipeMissingPrerequisiteCount == 0`,
  - both pass entries appear in `RenderGraphCommandPassStats` with `Recorded` status,
  - the imported `Backbuffer` is transitioned to `TextureLayout::Present` as the end-of-graph sentinel and (if a preceding `Undefined → ColorAttachment` barrier is emitted) the `ColorAttachment → Present` transition occurs afterwards.

  The runtime-driven assertions originally requested by this task — `CreateReferenceEngineConfig()` driving the frame, GRAPHICS-029B's reference triangle, and the `RuntimeRenderExtractionStats::ProceduralGeometryUploads == 1` assertion — are explicitly deferred. Driving the test through `Runtime::Engine` would couple a `contract;graphics` slice to the runtime extraction layer (the procedural-geometry stat lives in `Extrinsic.Runtime.RenderExtraction`). The renderer-side contract is fully exercised by the counters and the recorded-status / barrier-sequence assertions above, mirroring the precedent set by `Test.MinimalDebugSurfacePass.cpp::RendererRoutesAndIncrementsExecutionsCounter`. The runtime-integrated assertion is recorded as a follow-up bullet in the Completion section.

## Tests
- [x] `contract;graphics` test: pass-class direct invocation records `BindPipeline(present)` + `Draw(3, 1, 0, 0)` in order and only those two commands; the empty-pipeline case records no events. (`Test.MinimalDebugPresentPass.cpp::ExecuteRecordsBindPipelineThenFullscreenDrawInOrder`)
- [x] `contract;graphics` test (renderer-integrated): with `FrameRecipeKind::MinimalDebug` selected the `Pass.Present.MinimalDebug` pass routes to `Recorded` and bumps `MinimalPresentPassExecutions` by 1, while the default-recipe passes do not appear. (`RendererRoutesAndIncrementsPresentExecutionsCounter`)
- [x] `contract;graphics` test: forcing the slot-0 pipeline lease to fail (`FailPipelineCreateCall = 3`) routes the present pass to `SkippedUnavailable` and bumps `MinimalRecipeMissingPrerequisiteCount`. (`MissingSlotZeroPipelineLeaseSkipsUnavailableAndIncrementsCounter`)
- [x] `contract;graphics` test: flipping the device to non-operational between `Initialize` and `ExecuteFrame` routes the present pass to `SkippedNonOperational` and leaves `MinimalPresentPassExecutions` at zero. (`NonOperationalDeviceSkipsNonOperational`)
- [x] `contract;graphics` test: with the default recipe selected, the new branch is not hit and `MinimalSurfacePassExecutions == 0`, `MinimalPresentPassExecutions == 0` (recipe-vs-default isolation). (`DefaultRecipeDoesNotIncrementMinimalPresentCounter`)
- [x] `contract;graphics` test (acceptance driver): one frame of the minimal-debug-surface recipe co-increments both pass counters to 1, leaves the prerequisite counter at zero, and emits the backbuffer end-of-graph transition to `TextureLayout::Present`. (`AcceptanceFrameRecordsBothPassesAndBarrierSequence`)
- Out of scope for this task: no `gpu`/`vulkan` test in this slice (covered by GRAPHICS-032D).
- Deferred to a follow-up `contract;runtime` slice (records the runtime-extraction half of the acceptance contract): drive the minimal recipe through `Runtime::Engine` with a procedural renderable and assert `RuntimeRenderExtractionStats::ProceduralGeometryUploads == 1`.

## Docs
- [x] Update `src/graphics/renderer/README.md` to list the new `Pass.Present.MinimalDebug` module, describe how `MinimalPresentPassExecutions` increments, and document that the missing-prerequisite counter now accumulates from a third record site (the shared slot-0 lease feeding the present pass).
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module (total: 429 modules; `graphics/renderer` count 51 → 52).
- `docs/architecture/rendering-three-pass.md` was reviewed; the minimal-recipe pass-name table does not currently enumerate per-pass labels, so no edit was required.

## Acceptance criteria
- [x] After this slice, on the Null device, the `MinimalDebugSurface` frame contract is fully exercised through CPU mocks via the `Test.MinimalDebugPresentPass.cpp` contract suite and the `AcceptanceFrameRecordsBothPassesAndBarrierSequence` driver. Once `GRAPHICS-033C` lands the Vulkan command-recording bodies, the same recipe will target a real swapchain.
- [x] `MinimalSurfacePassExecutions` and `MinimalPresentPassExecutions` increment together for one frame (asserted by the acceptance driver).
- [x] No regression in default-recipe behavior or counters (asserted by `DefaultRecipeDoesNotIncrementMinimalPresentCounter`; `Test.RendererFrameLifecycle.cpp` already asserts the default-recipe `CullingPass`/`DepthPrepass` `Recorded` status and the remaining-pass `SkippedUnavailable` taxonomy, neither of which this slice modifies).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding a `gpu;vulkan` test.
- Reusing the imported `Backbuffer` for non-present writes (validated by render-graph rejection).
- Wiring the default recipe through the minimal pass branches.

## Completion
- Completed: 2026-05-13.
- Commit reference: `e50c593` ("GRAPHICS-032C: wire MinimalDebugPresent CPU-mock pass body") via PR #823 from `claude/inspect-engine-state-4lqQS`, merged to `main` at 2026-05-13T21:52:41Z.
- Verification:
  - Project CI ran on PR #823 (`ci` preset, clang-20 toolchain) and passed before merge to `main`.
  - Authoring session ran the structural checks locally; the focused `cmake --preset ci` / `ctest -L contract` gate ran in the PR's CI environment because the authoring container shipped clang-18 only (`.claude/setup.sh` toolchain provisioning was network-blocked by `403 Forbidden` on the launchpad PPAs).
  - `docs/api/generated/module_inventory.md` was regenerated as part of the PR (graphics/renderer module count 51 → 52; total 428 → 429).

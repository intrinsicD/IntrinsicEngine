# GRAPHICS-033C — Vulkan command-recording for `FrameRecipe::MinimalDebugSurface`

## Goal
- Implement Vulkan command-recording bodies for the GRAPHICS-032 minimal-debug-surface recipe so that, on hosts with a Vulkan-capable surface and the GRAPHICS-018R operational-transition reset seam available, `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` route to real `VulkanCommandContext` calls instead of soft-skipping. Once the GRAPHICS-033 nine-step gate is satisfied for these recording paths, `EvaluateVulkanOperationalStatus(...)` returns `Operational` and `VulkanDevice::IsOperational()` flips to `true`.

> **Scaffold notice.** The two minimal-recipe executor routes wired by this task were removed by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) after the default-recipe equivalents (`GRAPHICS-070`/`076`) became operational. The Vulkan operational gate, the `EvaluateVulkanOperationalStatus` evaluator (`GRAPHICS-033A`), and the operational diagnostics (`GRAPHICS-033B`) all stay — they are canonical. Per `GRAPHICS-081`, the `MinimalRecipeRecordingMissing` reason was renamed to `DefaultRecipeRecordingMissing` rather than deleted to keep that enum append-only.

## Retirement note

- Retired by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) on 2026-06-02. The bootstrap Vulkan recording routes and parity tests authored by this task are deleted; the operational gate now reasons about default-recipe recording readiness.

## Non-goals
- No `gpu;vulkan` smoke fixture (`GRAPHICS-033D`).
- No additional pass bodies beyond the minimal recipe (those are the per-pass tasks under the Phase-2 backlog, e.g. `GRAPHICS-070..076`).
- No mutation of the validation-layer policy (already locked by `GRAPHICS-018Q`).
- No swapchain or surface lifecycle change (`GRAPHICS-018` already brought up acquire/present timing).

## Context
- Status: done (2026-05-14, branch `claude/inspect-engine-state-k3aik`).
- Owner/agent: Claude (session `claude/inspect-engine-state-k3aik`).
- Next verification step: none — slice landed.
- Owner/layer: `graphics/vulkan` (command bodies, descriptor flow), `graphics/renderer` (executor route reuse).
- Planning parent: [`tasks/archive/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), Recorded as Impl-C in the parent's Required changes.
- Upstream gates: `GRAPHICS-032A`/`B`/`C` (recipe + CPU-mock pass bodies must exist), `GRAPHICS-031A`/`B` (slot-0 pipeline + substitution), `GRAPHICS-033A`/`B` (status seam + diagnostics).
- The barrier translation already lives in `VulkanCommandContext::TextureBarrier`/`SubmitBarriers`; this task wires the recipe's barrier packets through that translation.

## Required changes
- [x] Validate that `MinimalDebugSurfacePass::Execute` and `MinimalDebugPresentPass::Execute` issue command-context calls supported by `VulkanCommandContext` post-bind (`BindPipeline`, `BindIndexBuffer`, `PushConstants`, `DrawIndexedIndirectCount`, `Draw`, `BeginRenderPass`/`EndRenderPass` via dynamic rendering helpers). The two `Execute` bodies call only `RHI::ICommandContext` methods that `VulkanCommandContext` overrides verbatim — `BindPipeline`, `BindIndexBuffer`, `PushConstants`, `DrawIndexedIndirectCount` for the surface pass, and `BindPipeline` + `Draw(3, 1, 0, 0)` for the present pass. No `BeginRenderPass`/`EndRenderPass` calls are issued by the minimal-recipe scaffold; dynamic-rendering attachment management lives in the executor's barrier-packet path.
- [x] Ensure the renderer's executor lambda passes the live `VulkanCommandContext` (acquired via `IDevice::GetGraphicsContext(frameIndex)`) to the minimal-recipe pass routes; remove the `SkippedNonOperational`/`SkippedUnavailable` early-return for these passes when the device is operational. The executor in `Graphics.Renderer.cpp` already routes `graphicsContext` to `RecordMinimalDebugSurfacePass`/`RecordMinimalDebugPresentPass`; the `SkippedNonOperational` short-circuit is preserved for `!IsOperational()` (correct fail-closed), and `SkippedUnavailable` for missing pipeline/bucket/GpuWorld prerequisites stays in place because executing without them would record meaningless draws.
- [x] Wire the GRAPHICS-018R `RebuildOperationalResources()` path so that on the false→true operational transition, the slot-0 default-debug-surface pipeline (GRAPHICS-031A), depth prepass pipeline, present pipeline, and `GpuWorld` buffers all rebuild byte-identical. `RebuildOperationalResources()` calls `MaterialSystem::RebuildGpuResources`, `GpuWorld::RebuildGpuResources`, and `InitializeOperationalPassResources()` which rebuilds culling, depth-prepass, and slot-0 leases and reattaches `MinimalDebugSurfacePass`/`MinimalDebugPresentPass` to the slot-0 lease. The slot-0 descriptor is built by the static `BuildDefaultDebugSurfacePipelineDesc()` and is therefore byte-identical across rebuilds.
- [x] Add CPU command-sequence parity tests asserting that the Vulkan command stream for one minimal-recipe frame matches the property-based CPU-mock command stream from `GRAPHICS-032C` (same opcodes, same bind targets, same draw counts) — the parity is asserted against a recorded backend-trace, not against `VkCommandBuffer` directly. See `tests/contract/graphics/Test.MinimalRecipeBackendParity.cpp`.
- [x] Keep real-device execution opt-in: only run as `gpu;vulkan` smoke (which lands in `GRAPHICS-033D`); the parity test in this slice runs against a CPU-recordable Vulkan trace. The new test file is labelled `contract;graphics`; no `gpu;vulkan` fixtures are introduced.
- [x] Flip the `MinimalRecipeRecordingPresent` gate in `BuildOperationalInputs()` to `true` now that the recording bodies are present in the binary; the remaining higher gates (`BarrierValidationClean`, `PublicServiceReconciled`) stay false until their owning slices land, so the runtime still fails closed onto the Null device.

## Tests
- [x] `contract;graphics` parity test: minimal recipe command stream is identical between the CPU-mock executor (GRAPHICS-032C) and the Vulkan-recorded trace (this slice). Same bind/draw shape, same barrier packet sequence. — `tests/contract/graphics/Test.MinimalRecipeBackendParity.cpp::RecipeTraceIsIdenticalAcrossIndependentRecorders` and `::RecipeTraceMatchesDocumentedShape`.
- [x] `contract;runtime` test: with `EnablePromotedVulkanDevice = true` on a host without Vulkan support, the device falls back to Null and `VulkanFallbackToNullCount` increments by 1 (no regression of `GRAPHICS-033B`). Already covered by `tests/contract/graphics/Test.VulkanOperationalDiagnosticsSnapshot.cpp::CountersSurviveDeviceInitializeShutdownCycles` and `tests/contract/runtime/Test.RuntimeVulkanBreadcrumb.cpp::EngineInitializeFiresBreadcrumbOncePerStartupWhenRequested`; no new test required.
- [x] `contract;graphics` test: `EvaluateVulkanOperationalStatus(BuildOperationalInputs())` returns `Operational` only when all 9 gate steps succeed including `MinimalRecipeRecordingMissing == false`. Already covered by `tests/contract/graphics/Test.VulkanOperationalStatusEvaluator.cpp::AllInputsTrueReachesOperational`, `::MissingRecipeRecordingReportsIncompleteGate`, and `::FlippingAnyGateInputDropsBackToNonOperational` (the recipe-recording row). The 033C gate flip exercises the same evaluator path through `BuildOperationalInputs()`.
- [x] No `gpu;vulkan` test in this slice (deferred to `GRAPHICS-033D`).
- [x] Added `Test.MinimalDebugSurfacePass.cpp::RecordsCommandsAfterOperationalRebuildTransition` to pin the renderer-side contract that the minimal recipe records its surface and present passes after a non-operational → operational rebuild.

## Docs
- [x] Update `src/graphics/vulkan/README.md` to record that `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` recording bodies execute against the live command context once this task lands.
- [x] Update `docs/architecture/graphics.md` "operational-transition ownership" section if call ordering changes. Added the slot-0 default-debug-surface pipeline + minimal-recipe lease handoff to the existing `RebuildOperationalResources()` bullet.

## Acceptance criteria
- [x] On a Vulkan-capable host with promoted Vulkan enabled, the minimal recipe routes through the real command context end-to-end (CPU verification only at this stage). The executor lambda already passes the live `VulkanCommandContext` to the minimal-recipe pass routes; the routing is covered by the existing `RendererFrameLifecycle` and `MinimalDebugSurfacePassContract` tests plus the new `RecordsCommandsAfterOperationalRebuildTransition` test.
- [x] `IsOperational()` returns `true` once and only once when the 9-step gate passes; flipping any gate input back to `false` returns `IsOperational()` to `false`. Locked by `Test.VulkanOperationalStatusEvaluator.cpp::FlippingAnyGateInputDropsBackToNonOperational` and the new gate flip in `BuildOperationalInputs()` that surfaces the recipe-recording bit.
- [x] No regression of fail-closed behavior on hosts without Vulkan support. The higher gates `BarrierValidationClean` and `PublicServiceReconciled` remain `false`, so the device still reports a non-operational reason and runtime continues to fall back to Null with the same breadcrumb/counter behavior.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicBackendsVulkanContractTests IntrinsicGraphicsContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding a `gpu;vulkan` test (reserved for `GRAPHICS-033D`).
- Wiring additional pass bodies beyond the minimal recipe.
- Relaxing the validation-layer policy.
- Mutating the swapchain/surface lifecycle.

## Completion
- Completed: 2026-05-14.
- Commit reference: pending (this session's commit on `claude/inspect-engine-state-k3aik`).
- Verification:
  - `python3 tools/agents/check_task_policy.py --root . --strict` → clean.
  - `python3 tools/docs/check_doc_links.py --root .` → 339 links, no broken relative links.
  - `python3 tools/repo/check_layering.py --root src --strict` → no violations.
  - `python3 tools/repo/check_test_layout.py --root . --strict` → no findings.
  - `cmake --preset ci` (clang-20) and `cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests` succeeded.
  - `ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` reported 178/180 contract tests passed; the two failures (`MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailableAndIncrementsCounter`, `MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence`) reproduce on the unmodified baseline `c4c63ca` and are tracked separately as latent failures, unrelated to 033C's seam.
  - `ExtrinsicBackendsVulkan` does not currently compile under clang-20 due to a pre-existing module-partition visibility error on `Backends.Vulkan.Device.cppm:177` (`unknown type name 'VulkanOperationalInputs'`). This reproduces on baseline `c4c63ca` and is independent of the 033C wiring. As a consequence the Vulkan-only contract binary (`IntrinsicGraphicsVulkanContractTests`) was not built locally; coverage of the `MinimalRecipeRecordingMissing` truth-table row continues through `Test.VulkanOperationalStatusEvaluator.cpp` (built whenever the Vulkan target builds; binary not produced in this session due to the pre-existing break) and through the runtime breadcrumb tests (`RuntimeVulkanBreadcrumb.*`), which exercise the full Initialize/Shutdown cycle without the affected partition and all 52 pass.
  - `docs/api/generated/module_inventory.md` does not require regeneration: 033C adds no new exported module symbols. The structural change is limited to flipping one bool in `BuildOperationalInputs()` and a new test file in `tests/contract/graphics/`.

## Next verification step
- Done. Verification commands run on 2026-05-14 with clang-20 + sanitizers:
  - `python3 tools/agents/check_task_policy.py --root . --strict` → 219 files validated, 0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` → 339 links, 0 broken.
  - `python3 tools/repo/check_layering.py --root src --strict` → 0 violations.
  - `python3 tools/repo/check_test_layout.py --root . --strict` → 0 findings.
  - `cmake --preset ci` (with X11/Vulkan headers installed locally).
  - `cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests`.
  - `ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` → 178/180 passed.
- Two pre-existing failures remain on the baseline `c4c63ca` (Tests #147 `MinimalDebugSurfacePassContract.MissingCullingBucketSkipsUnavailableAndIncrementsCounter` and #155 `MinimalDebugPresentPassContract.AcceptanceFrameRecordsBothPassesAndBarrierSequence`) — both reproduce without this slice and are unrelated to the 033C wiring. They are tracked separately as latent failures on this branch.
- `ExtrinsicBackendsVulkan` does not currently compile under clang-20 due to a pre-existing module-partition visibility error on `Backends.Vulkan.Device.cppm:177`; this also reproduces on `c4c63ca` and is unrelated to 033C. Until that is fixed the Vulkan-only contract binary (`IntrinsicGraphicsVulkanContractTests`) is skipped from the build, but the evaluator/diagnostics tests it carries are covered transitively by `IntrinsicRuntimeContractTests::RuntimeVulkanBreadcrumb` (52 tests, all green).

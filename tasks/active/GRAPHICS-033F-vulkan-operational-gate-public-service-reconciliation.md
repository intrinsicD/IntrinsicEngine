# GRAPHICS-033F — Wire the `PublicServiceReconciled` operational gate

## Status

- Status: in-progress (slice 1 landed on this branch; awaiting CI gate confirmation for downstream `GRAPHICS-080` retirement).
- Owner/agent: Claude on `claude/remove-blocking-validations-LWwfI`.
- Branch: `claude/remove-blocking-validations-LWwfI`.
- Slice 1 landed in this branch:
  - `VulkanDevice::HasOperationalSafetyPrerequisites()` re-derived from raw, non-circular preconditions in `src/graphics/vulkan/Backends.Vulkan.Device.cpp`: `!m_DeviceLost`, `m_GlobalPipelineLayout != VK_NULL_HANDLE`, `m_BindlessHeap && m_BindlessHeap->IsValid()`, `m_TransferQueue && m_TransferQueue->IsValid()`, swapchain size-consistency, per-frame `CmdPool`/`CmdBuffer`/`Fence`/`ImageAcquired`/`RenderDone`, and every `m_CmdContexts[i].IsBound()`.
  - `VulkanCommandContext::IsBound()` added in `src/graphics/vulkan/Backends.Vulkan.CommandPools.cppm` as a backend-local inline predicate consumed by the safety-prereq check; no `Vk*` types cross any module boundary.
  - `VulkanDevice::BuildOperationalInputs()` now sets `inputs.PublicServiceReconciled = HasOperationalSafetyPrerequisites()` instead of the hard-coded `false`.
  - Service-diagnostics block at the bind-loop publish point in `Backends.Vulkan.Device.cpp` now sources `PublicBindlessHeapExposed` from `HasOperationalSafetyPrerequisites() && IsOperational()` rather than from `IsOperational()` alone, removing the implicit assumption that the operational predicate implied the safety prereqs.
  - Tests: `VulkanFailClosedContract.PublicServiceReconciledIsFalseOnFreshDevice`, `VulkanFailClosedContract.PublicServiceReconciledStaysFalseAfterNullWindowInitialize` confirm cold-start and post-`Shutdown` fail-closed semantics. The existing exhaustive 14-bit gate sweep (`Test.VulkanOperationalStatusEvaluator.cpp`) stays byte-identical.
  - Docs: `src/graphics/vulkan/README.md` §10 enumerates the raw preconditions consulted by gate 8 and records that `HasOperationalSafetyPrerequisites()` is the single source of truth feeding `PublicServiceReconciled`.
- Next verification step: run the default CPU gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) plus the focused `contract;graphics` and `IntrinsicRuntimeTests` targets, then the `gpu;vulkan` flipping-test on a Vulkan-capable host (reserved for `GRAPHICS-033D`).

## Goal
- Flip the `PublicServiceReconciled` input to the `EvaluateVulkanOperationalStatus(...)` 9-step gate from its current hard-coded `false` (`src/graphics/vulkan/Backends.Vulkan.Device.cpp:883`) so it observes a non-circular `VulkanDevice::HasOperationalSafetyPrerequisites()` predicate that reports `true` once the canonical public services — bindless heap, transfer queue, pipeline manager, swapchain/backbuffer import, and command contexts — are all live and consistent with each other. This is gate step 8 of the operational checklist in `src/graphics/vulkan/README.md:363–365` and is the second of the two remaining gates blocking `IsOperational()` from ever returning `true`.

## Non-goals
- No `gpu;vulkan` smoke fixture (still reserved for `GRAPHICS-033D`).
- No change to the `BarrierValidationClean` gate (`GRAPHICS-033E`).
- No new public services exposed on `IDevice`; this task reconciles existing services' readiness, it does not add new ones.
- No relaxation of any existing fail-closed counter or the per-counter rate-limit policy (`GRAPHICS-018Q`).
- No mutation of the GRAPHICS-018R `RebuildOperationalResources()` shape; the rebuild seam already exists.
- No change to runtime/app-side public service consumers (transfer queue users, bindless lookups, etc.) — runtime continues to branch only on `IDevice::IsOperational()`.

## Context
- Status: not started.
- Owner/layer: `graphics/vulkan` (this task is wholly backend-internal; `HasOperationalSafetyPrerequisites()` and `BuildOperationalInputs()` already live in `Backends.Vulkan.Device.cpp`).
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md). The parent identified Impl-A/B/C/D but did not enumerate an explicit child for gates 7/8; `GRAPHICS-033F` is the planning-gap fill that owns gate 8.
- Upstream gates: `GRAPHICS-033C` (recording bodies present and `MinimalRecipeRecordingPresent == true`), `GRAPHICS-033E` (gate 7 — `BarrierValidationClean`). Per the evaluator's first-failing-gate ordering, gate 7 must already be reconcilable before gate 8 becomes the deciding bit.
- Downstream gates: the active `GRAPHICS-080` retirement, which is blocked on gates 7 and 8 both flipping to `true`.
- Today `HasOperationalSafetyPrerequisites()` returns hard `false` (`Backends.Vulkan.Device.cpp:809–817`). The service diagnostics block at `Backends.Vulkan.Device.cpp:1814–1822` already computes `PublicBindlessHeapExposed`, `PublicTransferQueueExposed`, `PublicServicesExposed`, and `PublicServicesRemainFailClosed` — but `PublicBindlessHeapExposed` is gated on `IsOperational()`, which creates a definitional cycle through `BuildOperationalInputs()`. This task breaks the cycle by sourcing gate 8 from raw live-handle prerequisites instead of from the operational outcome.
- Layering: this is a backend-internal slice; no public surface change on `RHI::IDevice`. Runtime, renderer, and app continue to consume only `IsOperational()`.

## Required changes
- [ ] Re-derive `VulkanDevice::HasOperationalSafetyPrerequisites()` from raw, non-circular preconditions:
  - `m_GlobalPipelineLayout != VK_NULL_HANDLE`,
  - `m_BindlessHeap && m_BindlessHeap->IsValid()`,
  - `m_TransferQueue && m_TransferQueue->IsValid()`,
  - `m_Swapchain != VK_NULL_HANDLE` and the swapchain image / view / handle arrays are size-consistent,
  - every `PerFrame` slot has a non-null `CmdPool`, `CmdBuffer`, `Fence`, `ImageAcquired`, and `RenderDone`,
  - every `m_CmdContexts[i]` has been bound (use a single backend-local "all contexts bound" boolean already published as `serviceDiagnostics.CommandContextsRebound`),
  - the device is not lost (`!m_DeviceLost`).
- [ ] Update `VulkanDevice::BuildOperationalInputs()` to set `inputs.PublicServiceReconciled = HasOperationalSafetyPrerequisites()` instead of the hardcoded `false` at `Backends.Vulkan.Device.cpp:883`.
- [ ] Update the service-diagnostics publish step at `Backends.Vulkan.Device.cpp:1814–1822` to source `PublicBindlessHeapExposed` and `PublicServicesExposed` from `HasOperationalSafetyPrerequisites() && IsOperational()` rather than from `IsOperational()` alone, so the diagnostics flag flips `true` only after the device becomes operational AND the safety prereqs hold (no observable behavior change for fail-closed paths, but it removes the implicit assumption that `IsOperational()` implies safety prereqs).
- [ ] Reset all safety-prereq inputs on `VulkanDevice::Shutdown()` (already implicit because handles are zeroed); confirm via test that a post-`Shutdown` `BuildOperationalInputs()` reports `PublicServiceReconciled == false`.
- [ ] Document in `src/graphics/vulkan/README.md` §10 that gate 8 now consults `HasOperationalSafetyPrerequisites()` and enumerate the raw preconditions in the §10 description, replacing the current placeholder text about "stays false until the owning slice lands".

## Tests
- [ ] `contract;graphics` test: `EvaluateVulkanOperationalStatus(...)` with `PublicServiceReconciled = false` returns `{RequestedButIncompleteGate, PublicServiceReconciliationFailed}` (already covered by `Test.VulkanOperationalStatusEvaluator.cpp`); confirm that exhaustive 14-bit gate sweep stays byte-identical after this slice.
- [ ] `contract;graphics` test: with `MinimalRecipeRecordingPresent = true`, `BarrierValidationClean = true`, and every safety-prereq input true, `EvaluateVulkanOperationalStatus(...)` returns `{Operational, None}`. Already covered by the existing `AllInputsTrueReachesOperational` row; confirm the matrix sweep stays green after the producer wiring.
- [ ] `contract;graphics` Vulkan-only test: starting from a fresh `VulkanDevice::Initialize(...)` on a host without a Vulkan device, `BuildOperationalInputs().PublicServiceReconciled == false` (no regression in fail-closed).
- [ ] `contract;graphics` Vulkan-only test: with every required handle synthesized as valid via the existing CPU-only Vulkan contract harness, `HasOperationalSafetyPrerequisites()` returns `true` and `BuildOperationalInputs().PublicServiceReconciled == true`; nulling any single prereq flips it back to `false`. Mirror the GRAPHICS-033A flipping-test shape per gate input.
- [ ] `contract;graphics` Vulkan-only test: after `Shutdown()`, `BuildOperationalInputs().PublicServiceReconciled == false` even if the device was operational before shutdown.
- [ ] No `gpu;vulkan` test in this slice (reserved for `GRAPHICS-033D`).

## Docs
- [ ] Update `src/graphics/vulkan/README.md` §10 to enumerate the raw preconditions consulted by gate 8 (one bullet per: pipeline layout, bindless heap, transfer queue, swapchain consistency, per-frame command/sync, command-context binding, device-not-lost) and to record that `HasOperationalSafetyPrerequisites()` is the single source of truth feeding `PublicServiceReconciled`.
- [ ] Update `src/graphics/vulkan/README.md` to reflect that the service-diagnostics block sources `PublicBindlessHeapExposed` from `HasOperationalSafetyPrerequisites() && IsOperational()` (correctness clarification, not a behavior change).
- [ ] Confirm `docs/architecture/graphics.md` operational-readiness section requires no update beyond removing any "still hardcoded false" placeholder text introduced by `GRAPHICS-033C`.

## Acceptance criteria
- [ ] On the CPU/null path, no observable behavior change (Null device does not use `HasOperationalSafetyPrerequisites()`).
- [ ] On the Vulkan path, after `GRAPHICS-033E` lands AND a clean recipe compile is published, `EvaluateVulkanOperationalStatus(...)` returns `{Operational, None}` and `IsOperational()` flips to `true` for the first time on this branch.
- [ ] On the Vulkan path without a Vulkan device, `PublicServiceReconciled` stays `false` and the device continues to fall back to Null with the same `VulkanRequestedButNotOperational` breadcrumb behavior.
- [ ] No `Vk*` symbol exposure change on `RHI::IDevice`; no new public service exposed; no new fail-closed counter.
- [ ] No regression of `FallbackDiagnosticsSnapshot`, `VulkanOperationalDiagnosticsSnapshot`, or `VulkanServiceDiagnosticsSnapshot` semantics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests ExtrinsicBackendsVulkanContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Exposing any new `RHI::IDevice` public surface.
- Relaxing any fail-closed counter, breadcrumb cadence, or per-call rate limit (locked by `GRAPHICS-018Q`).
- Mutating `RebuildOperationalResources()` ordering (locked by `GRAPHICS-018R`).
- Flipping `BarrierValidationClean` in the same patch (reserved for `GRAPHICS-033E`).
- Adding a new operational reason or status code (the taxonomy is append-only and unchanged here).
- Mixing mechanical file moves with semantic refactors.

## Next verification step
- Land the safety-prereq re-derivation, run the focused `contract` ctest invocation above on a Vulkan-capable host, confirm `IsOperational()` flips to `true` once gates 7 and 8 are both satisfied while fail-closed hosts retain their existing breadcrumb behavior.

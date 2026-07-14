---
id: RUNTIME-137
theme: F
depends_on:
  - ARCH-009
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-137 — Async GPU readback helper + pooled destination for compute backends

## Status

- Retired on 2026-07-09 at `Operational`.
- PR: pending. Commit: pending local change.
- Runtime now owns `Extrinsic.Runtime.AsyncBufferReadback`, a pooled
  `Graphics.GpuTransfer` readback wrapper used by `KMeansGpuAsyncReadbacks` to
  drain labels/distances/centroids without `IDevice::ReadBuffer`.
- `JobService` now owns the `GpuQueue` participant registry. Engine installs
  one renderer frame-command bridge to `JobService::RecordGpuQueueFrameCommands`,
  drains `JobService::DrainGpuQueueCompletedTransfers()` during Maintenance, and
  shuts participants down through `JobService::ShutdownGpuQueueParticipants(...)`
  after unregistering the bridge and performing the required device-idle wait.
- `SandboxEditorUi` registers `RuntimeKMeansGpuJobQueue` through
  `engine.Jobs().RegisterGpuQueueParticipant(...)`; `Runtime.Engine.cppm` no
  longer exports `RuntimeGpuJobParticipant*` types or registration functions.

## Goal
- Provide a thin, ergonomic async-readback helper that geometry/method GPU
  compute backends adopt in place of the synchronous `RHI::IDevice::ReadBuffer`
  path, so a backend can drain its GPU results without a full-device
  `vkDeviceWaitIdle`, and pool the readback destination so repeated solves do not
  allocate a fresh host buffer per drain. The helper also becomes the runtime
  substrate for the `JobService` `GpuQueue` target so modules submit GPU-backed
  work through the kernel job seam rather than owning bespoke queues.

## Non-goals
- No change to the `RHI::IDevice::ReadBuffer` contract or the Vulkan backend's
  `vkDeviceWaitIdle` behavior for that call (it stays as the explicit-stall
  escape hatch).
- No rewrite of `Runtime.GpuReadbackJob`, `Graphics.GpuTransfer`, or the
  `RHI::ITransferQueue` async contract; this task composes them into a smaller
  surface, it does not redesign them.
- No conversion of `ProgressivePoissonGpuBackend` to the helper here (that is a
  METHOD-013 concern); this task ships the reusable helper + the k-means
  consumer is a later backend task.
- No new frames-in-flight or scheduler policy.

## Context
- Owning subsystem/layer: `runtime` (it composes `Runtime.GpuReadbackJob` +
  `Graphics.GpuTransfer` + the `DerivedJob` scheduler, all of which runtime may
  already see). No new dependency edges into `graphics/rhi`.
- Origin: `docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 1.
  The async machinery is already non-blocking end-to-end
  (`Runtime.GpuReadbackJob.cpp:214-269`, `Graphics.GpuTransfer.cpp:187-264`,
  `RHI.TransferQueue.cppm:44-48`), but the *easy default* `IDevice::ReadBuffer`
  is contractually a stall (`RHI.Device.cppm:145-147`,
  `Backends.Vulkan.Device.cpp:3889-3937`), so backends drift onto it — evidenced
  by `Runtime.ProgressivePoissonGpuBackend.cpp:595,1440-1442`.
- Two concrete deficits to close: (a) friction — adopting the async path today
  means wiring `GpuTransfer` + `GpuReadbackJob` + the `DerivedJob` scheduler by
  hand; (b) churn — `SubmitGpuReadbackJob` heap-allocates a fresh
  `std::vector<std::byte>` per submit (`Runtime.GpuReadbackJob.cpp:177-178,213`).
- This is the highest-leverage audit follow-up because it unblocks *every*
  geometry compute backend (the pending k-means backend included) from starting
  on the stalling pattern.
- ARCH-013 re-review (2026-07-08): Decision re-scoped. `ARCH-009` retired with
  `JobService` and left `GpuQueue` execution explicitly deferred here; this
  task must provide/adapt the readback helper as that `GpuQueue` execution
  substrate, not as a second module-owned scheduler. The remaining explicit
  `RuntimeKMeansGpuJobQueue` participant path is also owned here: it should
  migrate behind the `JobService` GPU target or a narrow runtime service, not
  stay wired through `Engine`/editor-specific hooks.

## Required changes
- [x] Add a runtime helper (e.g. `Extrinsic.Runtime.AsyncBufferReadback`) that
      wraps enqueue → poll(`IsDelivered`) → deferred-collect over
      `Graphics.GpuTransfer::ScheduleReadback` / `Runtime.GpuReadbackJob`, taking
      a source `RHI::BufferHandle` + byte range and returning a ticket/handle the
      caller polls and collects without any `WaitIdle`.
- [x] Adapt/register the helper as the `JobService` `GpuQueue` target so module
      GPU work receives the same world-scoped cancellation, completion-gate, and
      maintenance semantics as CPU jobs.
- [x] Let the caller supply a reusable destination span/buffer (or pool the
      destination by a stable key) so repeated readbacks reuse host storage
      instead of allocating a `std::vector` per submit.
- [x] Route the helper's barrier as `ShaderWrite → TransferRead` on the source
      and keep the collect on the render/main thread per the existing transfer
      contract; do not introduce a new stall.
- [x] Document the helper as the sanctioned readback path for compute backends;
      keep `IDevice::ReadBuffer` as the explicit-stall escape hatch.

## Tests
- [x] Default CPU-gate contract test: the helper enqueues a readback and reports
      not-ready without blocking on a null/non-operational device, and collects
      deterministically when the mock transfer marks delivery — no `WaitIdle`
      invoked on the mock device.
- [x] Contract test: the pooled/caller-supplied destination is reused across
      repeated submits (no per-submit allocation of a fresh destination).
- [x] Opt-in `gpu;vulkan` smoke: a recorded compute write drained through the
      helper returns the expected bytes and asserts the device is not idled by
      the drain (reuses an existing readback smoke harness where possible).
- [x] `ctest ... -LE 'gpu|vulkan|slow|flaky-quarantine'` default gate stays green.

## Docs
- [x] Document the helper in `src/runtime/README.md` and cross-link
      `docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 1 and
      `docs/migration/kmeans-gpu-vulkan-compute-proposal.md` §6.
- [x] Note in `docs/architecture/algorithm-variant-dispatch.md` that GPU backends
      should collect results through this helper, not `IDevice::ReadBuffer`.
- [x] Regenerate `docs/api/generated/module_inventory.md` if a module surface is added.

## Acceptance criteria
- [x] A geometry/method GPU backend can drain results through one small helper
      call with no `vkDeviceWaitIdle` and no per-drain host allocation.
- [x] Module GPU jobs can route through the `JobService` `GpuQueue` target
      without owning a bespoke render-thread/readback queue.
- [x] Default CPU gate proves the enqueue/poll/collect contract on a
      non-operational device with honest not-ready reporting.
- [x] No new layering violation; runtime keeps composing existing facilities.
- [x] `IDevice::ReadBuffer` behavior is unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AsyncBufferReadback|GpuReadback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
# Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'AsyncBufferReadback|GpuReadback|BufferReadback' -L 'gpu' --timeout 120
```

Completed verification (2026-07-09):

```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeJobService.*:AsyncBufferReadback.*:GpuReadbackJob.*:KMeansGpuBackend.*:SandboxEditorUi.KMeansVulkanRequestQueuesGpuJobWhenSurfaceAccepts'
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
build/ci/bin/IntrinsicRuntimeIntegrationTests --gtest_filter='RuntimeEngineLayering.*'
ctest --test-dir build/ci --output-on-failure -R 'AsyncBufferReadback|GpuReadback|RuntimeJobService|KMeansGpuBackend|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'AsyncBufferReadback|GpuReadback|BufferReadback|KMeansGpu' -L 'gpu' -L 'vulkan' --timeout 120
```

Results: focused runtime GTest passed 33/33; `RuntimeEngineLayering.*` passed
11/11; focused CTest passed 169/169; full default CPU-supported CTest passed
3640/3640; opt-in `gpu;vulkan` smoke passed 5/5.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing `IDevice::ReadBuffer`'s stall contract or removing it.
- Adding a new device-wide stall on the async path.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (opt-in `gpu;vulkan` drain
  smoke); `CPUContracted` everywhere else via the enqueue/poll/collect contract
  test on a non-operational device.

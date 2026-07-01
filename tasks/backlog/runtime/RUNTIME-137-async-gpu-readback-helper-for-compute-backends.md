---
id: RUNTIME-137
theme: F
depends_on: []
---
# RUNTIME-137 — Async GPU readback helper + pooled destination for compute backends

## Goal
- Provide a thin, ergonomic async-readback helper that geometry/method GPU
  compute backends adopt in place of the synchronous `RHI::IDevice::ReadBuffer`
  path, so a backend can drain its GPU results without a full-device
  `vkDeviceWaitIdle`, and pool the readback destination so repeated solves do not
  allocate a fresh host buffer per drain.

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

## Required changes
- [ ] Add a runtime helper (e.g. `Extrinsic.Runtime.AsyncBufferReadback`) that
      wraps enqueue → poll(`IsDelivered`) → deferred-collect over
      `Graphics.GpuTransfer::ScheduleReadback` / `Runtime.GpuReadbackJob`, taking
      a source `RHI::BufferHandle` + byte range and returning a ticket/handle the
      caller polls and collects without any `WaitIdle`.
- [ ] Let the caller supply a reusable destination span/buffer (or pool the
      destination by a stable key) so repeated readbacks reuse host storage
      instead of allocating a `std::vector` per submit.
- [ ] Route the helper's barrier as `ShaderWrite → TransferRead` on the source
      and keep the collect on the render/main thread per the existing transfer
      contract; do not introduce a new stall.
- [ ] Document the helper as the sanctioned readback path for compute backends;
      keep `IDevice::ReadBuffer` as the explicit-stall escape hatch.

## Tests
- [ ] Default CPU-gate contract test: the helper enqueues a readback and reports
      not-ready without blocking on a null/non-operational device, and collects
      deterministically when the mock transfer marks delivery — no `WaitIdle`
      invoked on the mock device.
- [ ] Contract test: the pooled/caller-supplied destination is reused across
      repeated submits (no per-submit allocation of a fresh destination).
- [ ] Opt-in `gpu;vulkan` smoke: a recorded compute write drained through the
      helper returns the expected bytes and asserts the device is not idled by
      the drain (reuses an existing readback smoke harness where possible).
- [ ] `ctest ... -LE 'gpu|vulkan|slow|flaky-quarantine'` default gate stays green.

## Docs
- [ ] Document the helper in `src/runtime/README.md` and cross-link
      `docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 1 and
      `docs/migration/kmeans-gpu-vulkan-compute-proposal.md` §6.
- [ ] Note in `docs/architecture/algorithm-variant-dispatch.md` that GPU backends
      should collect results through this helper, not `IDevice::ReadBuffer`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if a module surface is added.

## Acceptance criteria
- [ ] A geometry/method GPU backend can drain results through one small helper
      call with no `vkDeviceWaitIdle` and no per-drain host allocation.
- [ ] Default CPU gate proves the enqueue/poll/collect contract on a
      non-operational device with honest not-ready reporting.
- [ ] No new layering violation; runtime keeps composing existing facilities.
- [ ] `IDevice::ReadBuffer` behavior is unchanged.

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

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing `IDevice::ReadBuffer`'s stall contract or removing it.
- Adding a new device-wide stall on the async path.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (opt-in `gpu;vulkan` drain
  smoke); `CPUContracted` everywhere else via the enqueue/poll/collect contract
  test on a non-operational device.

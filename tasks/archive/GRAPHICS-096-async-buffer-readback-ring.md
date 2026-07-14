---
id: GRAPHICS-096
theme: B
depends_on: [GRAPHICS-095]
maturity_target: Operational
---
# GRAPHICS-096 — Async GPU→CPU buffer readback ring on ITransferQueue

## Goal
- Add a non-blocking GPU→CPU buffer readback path to `RHI::ITransferQueue` that
  mirrors the existing async upload guarantee (no caller thread ever blocks on a
  GPU fence): `DownloadBuffer(...)` queues a device→host copy into a recycled
  host-visible readback staging ring and delivers the bytes on the once-per-frame
  `CollectCompleted()` drain, so algorithms and users can read GPU-computed
  buffer data back to the CPU repeatably and without a device stall.

## Non-goals
- No texture readback (owned by GRAPHICS-097).
- No high-level barrier facade (owned by GRAPHICS-098).
- No removal of the legacy `IDevice::ReadBuffer` smoke helper in this task; new
  code uses the ring, the legacy helper retires under a later cleanup.
- No persistently-mapped / ReBAR synchronous readback (deferred placement
  optimization per ADR-0023).

## Context
- Owning subsystem/layer: `src/graphics/rhi/` (abstraction) +
  `src/graphics/vulkan/` (first operational backend); Null device fail-closed.
- Today the only download primitive is `IDevice::ReadBuffer`
  (`src/graphics/rhi/RHI.Device.cppm:156-163`): it requires the destination to be
  `HostVisible=true` + `TransferDst`, the Vulkan backend `WaitIdle()`s on entry
  (full device stall), and it silently no-ops on backends without host-visible
  support. It is documented as a `gpu;vulkan` smoke helper, not a general path.
- Every shipping readback is a bespoke per-frame drain that re-implements
  staging + transition + copy + drain: `Picking.Readback` via
  `SelectionSystem::PublishPickResult()` and the post-process histogram via
  `PostProcessSystem` (see `docs/architecture/rendering-three-pass.md`
  "Pass-level Readback Drains"). There is no reusable ring.
- The upload side already establishes the model to mirror: `UploadBuffer`
  returns a `TransferToken`, `IsComplete()` polls atomically from any thread, and
  `CollectCompleted()` is the single render-thread, once-per-frame fence-wait +
  staging-reclaim point (`src/graphics/rhi/RHI.TransferQueue.cppm`).
- Builds on GRAPHICS-095 for sub-range validation of the readback region.
- ADR-0023 records the design, the `ReadbackToken` + `ReadbackSink`
  (drain-time `std::span<const std::byte>` callback, with a fixed-size
  destination-pointer convenience overload) shape, and the rejected alternatives.

## Slice plan
- **Slice A (CPU contract).** Add the `ReadbackToken`, `ReadbackSink`, and
  `ITransferQueue::DownloadBuffer(...)` surface plus diagnostics counters.
  Implement fail-closed on the Null device (invalid token) and provide a CPU
  mock transfer queue that delivers staged bytes through the sink on
  `CollectCompleted()`. Validate the readback region through GRAPHICS-095.
  Preserves the default CPU gate. Defers all real GPU staging to Slice B.
- **Slice B (Vulkan ring + smoke).** Implement the host-visible readback staging
  ring in the Vulkan backend: allocate-or-reuse readback buffers, record the
  device→host `CopyBuffer` on the transfer/graphics queue, signal the transfer
  timeline, copy out to the sink when the fence is observed complete inside
  `CollectCompleted()`. Add an opt-in `gpu;vulkan` smoke that round-trips a
  device-computed buffer to the CPU through the ring with no `WaitIdle`.

## Execution plan
- Keep the public RHI change append-only on `ITransferQueue`: existing upload,
  poll, and drain slots stay in their current order; new readback virtuals get
  fail-closed defaults.
- Model readback delivery with `ReadbackSink` carrying an optional fixed-size
  destination span plus an optional drain-time callback. Delivery is attempted
  only after range validation and only once per accepted token.
- Use `RHI::ValidateBufferRange(...)` for CPU mock and Vulkan source-region
  validation, with invalid requests returning invalid tokens and incrementing
  dropped diagnostics.
- Reuse the Vulkan transfer timeline for readback tokens. The ring allocates or
  reuses mapped host-visible `TRANSFER_DST` buffers, records `src -> staging`
  copies, and copies bytes to the sink from `CollectCompleted()`.
- Keep legacy `IDevice::ReadBuffer` untouched; the new smoke exercises
  `IDevice::GetTransferQueue().DownloadBuffer(...)` directly.

## Required changes
- [x] Extend `src/graphics/rhi/RHI.TransferQueue.cppm`: `ReadbackToken`,
      `ReadbackSink`, `DownloadBuffer(BufferHandle src, std::uint64_t size,
      std::uint64_t offset, ReadbackSink sink)` returning a `ReadbackToken`, an
      `IsComplete(ReadbackToken)` poll, and the documented thread-safety /
      no-blocking invariants matching the upload virtuals. Append new virtuals
      after existing ones to avoid vtable-slot churn.
- [x] Validate the requested `(offset, size)` against the source `BufferDesc`
      through `RHI::BufferTransfer` (GRAPHICS-095); reject out-of-range with an
      invalid token + a dropped-readback counter.
- [x] Null device / CPU mock: fail-closed `DownloadBuffer` returning an invalid
      token; CPU mock used by tests delivers staged bytes on `CollectCompleted()`.
- [x] Vulkan backend (`src/graphics/vulkan/`): host-visible readback staging
      ring, device→host copy recording, transfer-timeline fence, drain-time
      copy-out, ring reuse + high-water tracking.
- [x] Add transfer diagnostics counters (downloads queued/completed/dropped,
      bytes staged, ring high-water) exposed through the existing transfer
      diagnostics surface.

## Tests
- [x] CPU contract `tests/contract/graphics/Test.TransferQueueReadback.cpp`
      (labels `contract;graphics`): invalid token on Null device; mock-queue
      drain delivers correct bytes through the sink exactly once on
      `CollectCompleted()`; out-of-range request is dropped with the counter
      incremented; `IsComplete` transitions only after the drain.
- [x] Opt-in `gpu;vulkan` smoke
      `tests/integration/graphics/Test.BufferReadbackGpuSmoke.cpp`
      (labels `gpu;vulkan;graphics`): a device-local buffer written/computed on
      the GPU is read back through the ring and matches expected bytes, with no
      `WaitIdle` on the caller path.
- [x] Default CPU gate stays green.

## Docs
- [x] Update `src/graphics/rhi/README.md` and `src/graphics/vulkan/README.md`.
- [x] Add a "GPU→CPU readback ring" section to `docs/architecture/graphics.md`
      alongside the existing readback-drain description.
- [x] Refresh `docs/api/generated/module_inventory.md` for the surface change.
- [x] Cross-link ADR-0023.

## Acceptance criteria
- [x] `DownloadBuffer` exists with the documented no-blocking / thread-safety
      contract; the Null device fail-closes; the CPU mock delivers via the sink
      on drain.
- [x] No caller thread blocks on a GPU fence; the only fence wait remains inside
      `CollectCompleted()`.
- [x] Region validation is routed through GRAPHICS-095; out-of-range fails closed.
- [x] Default-gate contract tests pass; the opt-in `gpu;vulkan` smoke is cited as
      run for `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'TransferQueueReadback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'BufferReadbackGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Completion note
- PR/commit: this retirement commit.
- 2026-06-22: Implemented and retired in the local `GRAPHICS-096` commit.
  Focused evidence: clean `ci` configure plus
  `cmake --build --preset ci --target IntrinsicGraphicsContractTests -- -j16`,
  `ctest --test-dir build/ci --output-on-failure -R 'TransferQueueReadback' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`,
  `cmake --preset ci-vulkan`,
  `cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests -- -j16`,
  and
  `ctest --test-dir build/ci-vulkan --output-on-failure -R 'BufferReadbackGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120`.

## Forbidden changes
- Blocking any caller thread on a GPU fence inside `DownloadBuffer`.
- Silent no-op / silent truncation on invalid requests (must fail closed).
- Adding ECS / runtime / asset-service knowledge to `graphics/rhi` or
  `graphics/vulkan`.
- Routing new readbacks through the legacy `IDevice::ReadBuffer` `WaitIdle` path.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `GRAPHICS-096`
  (this task's Slice B) via the cited `gpu;vulkan` smoke.

---
id: GRAPHICS-098
theme: B
depends_on: [GRAPHICS-096]
maturity_target: Operational
---
# GRAPHICS-098 — High-level GpuTransfer facade with correct barrier brackets

## Goal
- Add a graphics-layer `Graphics::GpuTransfer` facade that gives algorithm and
  user code repeatable, hard-to-misuse helpers for the two common transfer
  patterns — "upload a CPU range into a shader-readable device buffer" and "read
  a device buffer back to the CPU" — each emitting the correct barrier bracket
  (`TransferWrite → ShaderRead` for uploads, `TransferRead` for readbacks)
  **at the moment the transfer is actually complete**, so the missing-barrier
  class of bug (BUG-049) cannot recur *and* a caller can never observe a
  shader-readable buffer before the async upload has landed.

## Non-goals
- No new RHI surface: the facade composes existing seams (`BufferManager`,
  `ITransferQueue` including the GRAPHICS-096 readback ring, `ICommandContext`).
- No change to `GpuWorld`'s existing one-shot upload-barrier tracking; this
  facade is for algorithm/user transfers, not the managed geometry pipeline.
- No ECS/asset-service knowledge.
- No caller-thread blocking on a GPU fence (readiness is polled, not waited).

## Readiness contract (the core correctness rule)
- `ITransferQueue::UploadBuffer` is **asynchronous**: the destination is not
  shader-readable until the returned `TransferToken` reports `IsComplete()`
  (`RHI.TransferQueue.cppm`; `GpuAssetCache::Tick` only promotes pending uploads
  after `IsComplete`). Recording a `TransferWrite → ShaderRead` barrier on an
  `ICommandContext` does **not** make that async copy complete. Therefore the
  facade must NOT record the upload→read barrier eagerly on the async path.
- The facade exposes two clearly separated upload modes:
  - **Async upload** (default, via `ITransferQueue`): returns an `UploadTicket`
    wrapping the `TransferToken`. The facade tracks pending tickets and records
    the one-shot `TransferWrite → ShaderRead` barrier for a ticket **only after
    its token completes** (observed during the per-frame post-`CollectCompleted`
    drain), then marks the ticket ready. Consumers gate binding on
    `IsReady(ticket)` / `IsComplete(token)`; no caller thread blocks.
  - **In-command copy** (opt-in, caller supplies staging already valid on the
    same queue and a target `ICommandContext`): the facade records
    `CopyBuffer` + the `TransferWrite → ShaderRead` barrier on that same command
    timeline, where immediate bracketing IS correct because copy and barrier are
    ordered in one submission.

## Context
- Owning subsystem/layer: `src/graphics/renderer/` (a small graphics utility,
  sibling to `GpuWorld`); consumers are runtime/methods/algorithm code that may
  use the graphics public API. Recommended module name
  `Extrinsic.Graphics.GpuTransfer` at
  `src/graphics/renderer/Graphics.GpuTransfer.cppm`; final placement confirmed at
  architecture review.
- Today authors hand-wire create → set `BufferUsage` flags →
  `WriteBuffer`/`UploadBuffer` → insert the `TransferWrite → ShaderRead` buffer
  barrier (`ICommandContext::BufferBarrier`,
  `src/graphics/rhi/RHI.CommandContext.cppm:234-241`). BUG-049 shows one failure
  mode (omitted barrier); the async-completion gap (barrier recorded before the
  transfer-queue copy has landed) is the second, subtler one this facade closes.
- The async-completion gating reuses two existing patterns: the `TransferToken` /
  `IsComplete` poll (`RHI.TransferQueue.cppm`, the same signal `GpuAssetCache`
  gates on) and `GpuWorld`'s one-shot pending-barrier bookkeeping (the BUG-049
  fix). The facade combines them: track pending upload tickets, drain completed
  tokens after `CollectCompleted`, and emit each ticket's barrier exactly once on
  completion.
- For readback, the facade wraps the GRAPHICS-096 ring so a caller gets a CPU
  result delivered on the frame-boundary drain without touching tokens, staging,
  or the `TransferRead` bracket directly.
- ADR-0023 records this as the ergonomic layer over GRAPHICS-095/096/097.

## Slice plan
- **Slice A (CPU contract).** Facade surface + the readiness contract: async
  `ScheduleUpload(...) -> UploadTicket` with token-gated one-shot barrier
  emission on the post-`CollectCompleted` drain, `IsReady(ticket)`; the in-command
  copy variant with immediate same-timeline bracketing; readback helper over the
  GRAPHICS-096 ring + `TransferRead`; transfer diagnostics counters. Proven
  against a mock `ITransferQueue` (token completion controllable) and a recording
  mock `ICommandContext`. Preserves the CPU gate.
- **Slice B (operational evidence).** Reuse the GRAPHICS-096 `gpu;vulkan` smoke
  to drive one upload-then-readback round-trip through the facade on a
  Vulkan-capable host, asserting the upload barrier is observed only after the
  token completed.

## Required changes
- [ ] Add `Graphics.GpuTransfer.cppm` (+ `.cpp`) with:
      an async upload helper `ScheduleUpload(...)` that allocates/uses a
      device-local buffer, issues `ITransferQueue::UploadBuffer`, and returns an
      `UploadTicket{ TransferToken, BufferHandle, pending-barrier state }` —
      **without** recording any barrier yet; an in-command-copy upload helper that
      records `CopyBuffer` + `TransferWrite → ShaderRead` on a supplied
      `ICommandContext`; and a readback helper that submits a GRAPHICS-096
      `DownloadBuffer` with the `TransferRead` bracket and surfaces the result
      through a sink/future.
- [ ] Add a per-frame `DrainCompleted(ICommandContext&)` (or equivalent) step
      that, after the transfer queue's `CollectCompleted`, finds tickets whose
      `TransferToken` `IsComplete`, records their one-shot
      `TransferWrite → ShaderRead` barrier, and marks them ready — so the barrier
      is emitted on a real completion, never eagerly. Expose `IsReady(ticket)` /
      `IsComplete(token)` for consumers to gate binding without blocking.
- [ ] Validate ranges through `RHI::BufferTransfer` (GRAPHICS-095); fail closed.
- [ ] Add transfer diagnostics counters (uploads scheduled/ready, readbacks
      issued/delivered, barriers emitted, pending high-water) on a
      `GpuTransferDiagnostics` snapshot.
- [ ] Keep non-trivial bodies in the `.cpp`; no new dependency edges beyond
      `graphics/rhi` + existing graphics seams.

## Tests
- [ ] CPU contract `tests/contract/graphics/Test.GpuTransferFacade.cpp`
      (labels `contract;graphics`):
      - async `ScheduleUpload` records **no** barrier while the mock token is
        incomplete and `IsReady` is false; after the token is marked complete and
        `DrainCompleted` runs, exactly one `TransferWrite → ShaderRead` barrier is
        recorded once and `IsReady` becomes true;
      - the in-command-copy helper records `CopyBuffer` + barrier immediately on
        the same mock context;
      - the readback helper drives the mock ring and delivers bytes once with the
        `TransferRead` bracket;
      - invalid ranges fail closed; diagnostics counters increment as specified.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/graphics/renderer/README.md` with the facade and its barrier
      contract; note it as the recommended path for algorithm/user transfers.
- [ ] Refresh `docs/api/generated/module_inventory.md`.
- [ ] Cross-link ADR-0023 and BUG-049.

## Acceptance criteria
- [ ] The async upload path emits the `TransferWrite → ShaderRead` barrier only
      after the `TransferToken` completes (never eagerly), exactly once per
      ticket; `IsReady(ticket)` is false until then and no caller thread blocks.
- [ ] The in-command-copy path emits `CopyBuffer` + the barrier on the same
      command timeline; the readback path always records the `TransferRead`
      bracket — both proven by the recording-mock contract test.
- [ ] Range validation is routed through GRAPHICS-095 and fails closed.
- [ ] Default-gate contract tests pass; operational evidence cites the reused
      GRAPHICS-096 `gpu;vulkan` round-trip.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GpuTransferFacade' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Introducing a new RHI surface (compose existing seams only).
- Recording the upload→read barrier eagerly on the async path before the
  `TransferToken` completes (the BUG-049-adjacent correctness hole this task
  exists to close).
- Blocking a caller thread on a GPU fence to make the upload "ready".
- Omitting the barrier bracket on any helper path.
- Adding ECS/asset-service knowledge to the graphics facade.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `GRAPHICS-098`
  (this task's Slice B) via the reused GRAPHICS-096 `gpu;vulkan` round-trip.

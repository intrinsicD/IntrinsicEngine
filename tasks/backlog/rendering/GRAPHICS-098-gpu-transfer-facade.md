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
  a device buffer back to the CPU" — each recording the correct barrier bracket
  (`TransferWrite → ShaderRead` for uploads, `TransferRead` for readbacks) so the
  missing-barrier class of bug (BUG-049) cannot recur at call sites.

## Non-goals
- No new RHI surface: the facade composes existing seams (`BufferManager`,
  `ITransferQueue` including the GRAPHICS-096 readback ring, `ICommandContext`).
- No change to `GpuWorld`'s existing one-shot upload-barrier tracking; this
  facade is for algorithm/user transfers, not the managed geometry pipeline.
- No ECS/asset-service knowledge.

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
  `src/graphics/rhi/RHI.CommandContext.cppm:234-241`). BUG-049 shows the failure
  mode: a device-buffer write path that omitted the upload→read barrier so
  consumers read stale data until unrelated state changed.
- For readback, the facade wraps the GRAPHICS-096 ring so a caller gets a CPU
  result delivered on the frame-boundary drain without touching tokens, staging,
  or the `TransferRead` bracket directly.
- ADR-0023 records this as the ergonomic layer over GRAPHICS-095/096/097.

## Slice plan
- **Slice A (CPU contract).** Facade surface + barrier recording against a
  recording mock `ICommandContext`; upload helper (create/usage/write +
  `TransferWrite → ShaderRead`) and readback helper (over the GRAPHICS-096 ring +
  `TransferRead`), plus transfer diagnostics counters. Preserves the CPU gate.
- **Slice B (operational evidence).** Reuse the GRAPHICS-096 `gpu;vulkan` smoke
  to drive one upload-then-readback round-trip through the facade on a
  Vulkan-capable host.

## Required changes
- [ ] Add `Graphics.GpuTransfer.cppm` (+ `.cpp`) with:
      an upload helper that allocates/uses a device-local buffer, writes via the
      transfer queue, and records the `TransferWrite → ShaderRead` barrier; and a
      readback helper that submits a GRAPHICS-096 `DownloadBuffer` with the
      `TransferRead` bracket and surfaces the result through a sink/future.
- [ ] Validate ranges through `RHI::BufferTransfer` (GRAPHICS-095); fail closed.
- [ ] Add transfer diagnostics counters (uploads/readbacks issued, barriers
      recorded) on a `GpuTransferDiagnostics` snapshot.
- [ ] Keep non-trivial bodies in the `.cpp`; no new dependency edges beyond
      `graphics/rhi` + existing graphics seams.

## Tests
- [ ] CPU contract `tests/contract/graphics/Test.GpuTransferFacade.cpp`
      (labels `contract;graphics`): the upload helper records exactly one
      `TransferWrite → ShaderRead` buffer barrier against a recording mock
      context; the readback helper drives the mock ring and delivers bytes once;
      invalid ranges fail closed; diagnostics counters increment as specified.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/graphics/renderer/README.md` with the facade and its barrier
      contract; note it as the recommended path for algorithm/user transfers.
- [ ] Refresh `docs/api/generated/module_inventory.md`.
- [ ] Cross-link ADR-0023 and BUG-049.

## Acceptance criteria
- [ ] The upload helper always records the `TransferWrite → ShaderRead` barrier;
      the readback helper always records the `TransferRead` bracket — proven by
      the recording-mock contract test.
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
- Omitting the barrier bracket on any helper path.
- Adding ECS/asset-service knowledge to the graphics facade.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `GRAPHICS-098`
  (this task's Slice B) via the reused GRAPHICS-096 `gpu;vulkan` round-trip.

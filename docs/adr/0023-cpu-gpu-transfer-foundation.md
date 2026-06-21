# ADR 0023: CPU↔GPU transfer foundation — async readback ring, buffer-transfer math, and a transfer facade

- **Status:** Proposed
- **Date:** 2026-06-21
- **Owners:** graphics (RHI + renderer) + runtime (consumers)
- **Related tasks:** GRAPHICS-095, GRAPHICS-096, GRAPHICS-097, GRAPHICS-098

## Context

The engine has a mature, non-blocking **CPU→GPU upload** path but no general
**GPU→CPU readback** path, and no shared helpers that make either direction
repeatable, validated, and hard to misuse for algorithm authors.

Current upload surface (solid):

- `RHI::IDevice::WriteBuffer(handle, data, size, offset)` /
  `WriteTexture(...)` — synchronous staged device-local writes
  (`src/graphics/rhi/RHI.Device.cppm:132-178`).
- `RHI::ITransferQueue::UploadBuffer / UploadTexture / UploadTextureFullChain`
  → `TransferToken`, polled with `IsComplete()`, staging reclaimed once per
  frame in `CollectCompleted()` on the render thread
  (`src/graphics/rhi/RHI.TransferQueue.cppm`). Thread-safe; no caller blocks on
  a GPU fence. This is the canonical async upload path.
- `RHI::TextureUpload` — backend-neutral, CPU-testable byte-size and
  packed-subresource layout math with alignment (`RequiredBufferOffsetAlignment`),
  the exemplar for CPU-contracted transfer math (`src/graphics/rhi/RHI.TextureUpload.cppm`).
- Lifetime/ownership: `BufferManager`/`TextureManager` ref-counted leases +
  deferred free; `GpuWorld` managed sub-allocation + compaction; `GpuAssetCache`
  residency state machine.
- Command primitives: `ICommandContext::CopyBuffer / CopyBufferToTexture /
  CopyTextureToBuffer / FillBuffer` and explicit `BufferBarrier / TextureBarrier
  / SubmitBarriers` (`src/graphics/rhi/RHI.CommandContext.cppm`).

Three gaps push on this foundation:

1. **No async GPU→CPU readback.** The only download primitive is
   `IDevice::ReadBuffer(handle, data, size, offset)`
   (`RHI.Device.cppm:156-163`), which is documented as a `gpu;vulkan`
   *smoke-test helper*: it requires the destination to be `HostVisible=true` +
   `TransferDst`, the Vulkan backend `WaitIdle()`s on entry (a full device
   stall), and it silently no-ops on backends without host-visible support.
   Every shipping readback today is a bespoke, fixed-purpose per-frame drain
   (`Picking.Readback` via `SelectionSystem`, histogram via `PostProcessSystem`),
   each re-implementing staging + transition + copy + drain by hand. An
   algorithm that computes on the GPU (compute geometry, reductions, baked
   fields) has no repeatable, non-stalling way to get results back.

2. **No CPU-testable buffer-transfer math/validation helper.** `RHI.TextureUpload`
   exists for textures, but there is no equivalent for buffers: sub-range
   validation, offset/size alignment, and partial-write region planning are
   re-derived ad hoc at each call site. ADR-0022 / RUNTIME-124's per-channel
   partial writes and the readback path below both want one shared, fail-closed
   contract for "is this (offset, size) legal against this buffer, and how is it
   aligned?".

3. **No high-level transfer facade.** Authors hand-wire
   create → set usage flags → `WriteBuffer`/`UploadBuffer` → insert
   `TransferWrite → ShaderRead` barrier (and for readback: transition → copy →
   read → wait). BUG-049 is a direct symptom: a geometry rebind path that wrote
   device buffers but omitted the upload→read barrier. There is no single helper
   that encapsulates the correct barrier brackets for "make this CPU array
   shader-readable" and "read this device buffer back to a CPU vector".

## Decision

Build the CPU↔GPU transfer foundation as three layered, independently
reviewable pieces, all backend-neutral at the RHI seam with Vulkan as the first
operational backend and the Null device fail-closed:

1. **`RHI::BufferTransfer` (GRAPHICS-095)** — a CPU-pure, backend-neutral
   math/validation module mirroring `RHI::TextureUpload`. Provides sub-range
   validation against a `BufferDesc`, copy/offset alignment helpers, and
   partial-write region planning, returning `Core::Expected`/fail-closed
   results. No backend dependency; verified entirely on the default CPU gate.
   This is the base layer both upload (RUNTIME-124) and readback (GRAPHICS-096)
   build on.

2. **Async GPU→CPU readback ring on `ITransferQueue` (GRAPHICS-096, headline).**
   Extend the existing async transfer abstraction with a download direction that
   mirrors the upload guarantee — *no caller thread blocks on a GPU fence*:

   - `ReadbackToken DownloadBuffer(BufferHandle src, size, offset, sink)` queues
     a copy from a device-local `src` into a recycled **host-visible readback
     staging ring** owned by the transfer queue, fenced like uploads.
   - Completion + delivery happen on the render thread inside the existing
     `CollectCompleted()` drain (the same frame-boundary drain pattern as
     `Picking.Readback`): when the fence for a readback is signaled, the queue
     copies the staged bytes to the caller's **sink** and marks the token
     complete. `IsComplete(token)` is an atomic poll from any thread.
   - The **sink** is a `ReadbackSink` delivering a `std::span<const std::byte>`
     callback at drain time (primary, matches the engine's drain idiom), with a
     plain destination-pointer convenience overload for fixed-size results.

   The Vulkan backend implements the ring (allocate-or-reuse host-visible
   `TransferSrc`-free readback buffers, record `CopyBuffer` device→host on the
   transfer/graphics queue, signal the transfer timeline, copy out on
   completion). The Null device fail-closes (invalid token). The legacy
   `IDevice::ReadBuffer` stays as the stalling test-only helper but new code uses
   the ring.

3. **Async texture readback (GRAPHICS-097).** `DownloadTexture` reusing
   `RHI::TextureUpload`'s packed-subresource layout *in reverse* to stage a
   `CopyTextureToBuffer` into the same readback ring, with the caller responsible
   for the source-layout transition (as `CopyTextureToBuffer` already requires).

4. **`Graphics::GpuTransfer` facade (GRAPHICS-098).** A graphics-layer
   convenience that encapsulates the correct barrier brackets so algorithm and
   user code do not hand-roll them: an "upload CPU range → shader-readable
   device buffer" helper that records the `TransferWrite → ShaderRead` barrier,
   and a "read device buffer → CPU" helper over the GRAPHICS-096 ring with the
   `TransferRead` bracket. It centralizes the pattern BUG-049 got wrong and adds
   transfer diagnostics counters. It composes existing seams (`BufferManager`,
   `ITransferQueue`, `ICommandContext`) and introduces no new RHI surface.

Robustness is uniform across the stack: transfer entry points validate through
`RHI::BufferTransfer`, fail closed with `Core::Expected`/invalid tokens rather
than silent no-ops, and expose monotonic diagnostics counters (downloads
queued/completed/dropped, bytes staged, ring high-water mark).

## Consequences

- Positive: a single, non-stalling, thread-safe readback path replaces bespoke
  per-feature drains; algorithm authors get repeatable upload/readback helpers
  with correct barriers; partial-upload work (RUNTIME-124) and readback share
  one validated buffer-range contract; the Null backend stays fail-closed and
  the default CPU gate covers every contract except the operational Vulkan ring.
- Trade-off: the readback ring adds host-visible staging memory and one more
  per-frame drain responsibility; readback latency is at least one frame (by
  design — the alternative is the existing `WaitIdle` stall). The legacy
  `ReadBuffer` stays temporarily to avoid churning the existing smokes; new code
  must not use it.
- Follow-up: GRAPHICS-095 (buffer math), GRAPHICS-096 (buffer readback ring,
  headline), GRAPHICS-097 (texture readback), GRAPHICS-098 (facade +
  diagnostics). RUNTIME-124 consumes GRAPHICS-095 for partial-write planning.

## Alternatives Considered

- **Keep `IDevice::ReadBuffer` + `WaitIdle` as the readback story.** Rejected as
  the foundation: a full device stall per readback defeats "fast", and the
  host-visible-only + silent-no-op contract defeats "robust". Retained only as a
  test-scoped helper.
- **One bespoke readback drain per feature (status quo).** Rejected: every
  feature re-implements staging + fence + drain (picking, histogram already do),
  which is exactly the duplication and the missing-barrier class of bug
  (BUG-049) this ADR removes.
- **Synchronous mapped readback (persistently mapped host-visible device
  buffers / ReBAR).** Orthogonal placement optimization, not a transfer model;
  a future per-buffer placement hint can sit on top of the ring (same framing as
  ADR-0022's deferred heap split). Not forked here.
- **A brand-new `IReadbackQueue` interface.** Rejected: readback shares staging
  lifetime, the frame-boundary drain, and thread-safety rules with uploads;
  extending `ITransferQueue` keeps one drain (`CollectCompleted`) and one
  injection seam (`GpuAssetCache`-style consumers inject `ITransferQueue&`).

## Validation

- GRAPHICS-095: CPU contract tests prove sub-range validation, alignment, and
  partial-write planning against representative `BufferDesc`s (default CPU gate).
- GRAPHICS-096/097: CPU/null contract tests prove fail-closed tokens, drain
  delivery against a mock transfer queue, and diagnostics counters; an opt-in
  `gpu;vulkan` smoke proves a device-computed buffer/texture round-trips to the
  CPU through the ring without `WaitIdle`.
- GRAPHICS-098: CPU contract tests prove the facade records the expected
  `TransferWrite → ShaderRead` / `TransferRead` barriers against a recording
  mock context; operational evidence reuses the GRAPHICS-096 `gpu;vulkan` smoke.

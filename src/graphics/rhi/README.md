# Graphics/RHI

This directory contains the `RHI` module/files.

## Contents

- `CMakeLists.txt`
- `RHI.CommandContext.cppm`
- `RHI.CommandContext.cpp` (out-of-line vtable key function + defaulted-virtual bodies)
- `RHI.BufferTransfer.cppm` / `.cpp`
- `RHI.Device.cppm`
- `RHI.FrameHandle.cppm`
- `RHI.QueueAffinity.cppm`
- `RHI.TimelineSemaphore.cppm`

## Queue affinity

- `RHI.QueueAffinity.cppm` declares the backend-neutral queue vocabulary
  `RHI::QueueAffinity { Graphics, AsyncCompute, Transfer }`,
  `QueueCapabilityProfile`, and `ResolveQueueAffinity(...)`.
- `Graphics` is the mandatory single-queue fallback. `AsyncCompute` and
  `Transfer` are optional capabilities; when absent, requested work demotes to
  `Graphics` and callers can count the demotion through their scheduling
  diagnostics.
- The RHI surface contains no backend submission policy. Framegraph partitioning
  and Vulkan queue recording consume this value contract without exposing
  API-native queue-family types through RHI.
- `RHI::IDevice::GetQueueCapabilityProfile()` reports optional queue support to
  backend-neutral schedulers. `IDevice::GetQueueContext(QueueAffinity,
  frameIndex)` is the command-recording seam for per-affinity command buffers;
  the default implementation falls back to `GetGraphicsContext(frameIndex)` so
  Null, CPU tests, and single-queue backends preserve existing behavior until
  they explicitly override a queue.
- `RHI::FrameQueueSubmitPlanDesc` is the backend-neutral bridge from a compiled
  framegraph submit plan to a concrete device. `BeginFrameQueueSubmitPlan(...)`
  lets a backend allocate per-batch command buffers for a frame, and
  `GetQueueSubmitContext(queue, frameIndex, batchIndex)` returns the matching
  command context for renderer recording. Default implementations decline the
  plan and route back to the single graphics context, so capability-absent hosts
  remain fail-closed.

## Timeline semaphores

- `RHI.TimelineSemaphore.cppm` declares `ITimelineSemaphore`, the backend-neutral
  signal/wait interface used by compiled cross-queue handoff records.
- The interface is intentionally minimal: `Signal(QueueAffinity, value)` records
  work completion on the producing queue, and `Wait(QueueAffinity, value)` gates
  the consuming queue. It does not expose Vulkan handles, queue-family indices,
  or backend submission ownership through RHI.
- The framegraph compiler owns deterministic value assignment and emits
  CPU-visible signal/wait/edge records. Concrete backend submission maps these
  records through `FrameQueueSubmitPlanDesc`; Vulkan lowers them to
  `vkQueueSubmit2` timeline waits/signals without exposing native handles
  through RHI.

## Placed resource memory

- `RHI::MemoryBlockHandle` is the backend-neutral handle for placed resource
  backing memory. The public `IDevice` seam exposes memory requirement queries
  for buffers/textures, opaque memory-block creation/destruction, placed
  buffer/texture creation, and placement introspection records. Requirements
  report byte size, required placement alignment, `MemoryTypeBits`, and whether
  a backend requires a dedicated allocation. `MemoryBlockDesc` repeats the
  selected maximum alignment so backends can allocate a block whose base address
  is compatible with every resource that will be placed inside it.
- Default `IDevice` implementations fail closed by returning invalid handles or
  empty requirement/info records. This keeps capability-absent backends and CPU
  mocks source-compatible until they implement real placement.
- The Null backend implements the contract as CPU bookkeeping for
  `GRAPHICS-118` Slice B: blocks select one compatible memory-type bit, placed
  resources validate alignment/range/compatibility, and overlapping placements
  are allowed because render-graph lifetime planning owns alias safety.
- Vulkan implements the placed allocation seam with backend-local memory blocks
  and placed buffer/image binding. The renderer consumes the seam for
  non-imported frame transients only when renderer transient aliasing is
  explicitly enabled and compiled aliasing reports a lower peak than the naive
  sum. The `GRAPHICS-118` opt-in Vulkan smoke proves the operational path; the
  renderer still falls back to ordinary per-resource allocations when a backend
  reports unsupported requirements, incompatible memory blocks, or a failed
  placed-resource bind.

## Transfer uploads and readbacks

- `RHI.TransferQueue.cppm` declares `ITransferQueue`, the async upload seam used
  by runtime/streaming paths. Buffer uploads and single-subresource texture
  uploads remain non-blocking. `UploadTextureFullChain(TextureHandle,
  std::span<const std::byte>)` is the GRAPHICS-018T seam for packed full-chain
  texture uploads; callers must pack bytes according to
  `ComputeFullChainUploadLayout()` and backends fail closed with an invalid
  `TransferToken` when metadata or byte counts are unsupported.
- `ITransferQueue::DownloadBuffer(BufferHandle, size, offset, ReadbackSink)` is
  the GRAPHICS-096 GPU-to-CPU buffer readback seam from
  [ADR-0023](../../../docs/adr/0023-cpu-gpu-transfer-foundation.md). It returns a
  `ReadbackToken`, validates the requested sub-range through
  `RHI.BufferTransfer`, and never blocks the caller thread on a GPU fence. Bytes
  are delivered from `CollectCompleted()` through `ReadbackSink` (fixed-size
  destination span and/or drain-time callback). Null/fallback backends fail
  closed with invalid tokens and dropped-readback diagnostics.
- `ITransferQueue::DownloadTexture(TextureHandle, TextureLayout, mip, layer,
  ReadbackSink)` is the GRAPHICS-097 texture readback seam over the same
  readback token/sink/drain contract. Callers must transition the source
  subresource into `TextureLayout::TransferSrc` before calling and own any
  after-readback transition. The RHI validation shape reuses
  `RHI.TextureUpload`'s packed-subresource layout in reverse: supported color
  `Tex2D` arrays and six-face cubemaps deliver exactly one mip/layer byte span,
  while depth-stencil, unsupported formats, invalid layouts, invalid sinks, and
  out-of-range subresources fail closed with dropped-readback diagnostics.
- `RHI.TextureUpload.cppm` owns backend-neutral texture upload and storage-size
  math: upload byte/block helpers, storage byte/block helpers used by
  framegraph transient placement, Vulkan-safe subresource offset alignment, and
  the layer-major / mip-minor `TextureUploadLayout` used by future batched
  transfer submissions.
- `RHI.BufferTransfer.cppm` owns backend-neutral buffer transfer math:
  strict sub-range validation against `BufferDesc`, division-based
  non-power-of-two alignment helpers, partial-write copy-region planning with
  optional coalescing, and property-agnostic element-count/component-size/stride
  dimension matching. It is CPU-only and is the shared validation primitive for
  upload, readback, and future property-to-buffer-range binding layers.

## Pipeline reload contracts

- `RHI.PipelineRegistry` owns CPU-testable pipeline identity and cache
  invalidation. `PipelineKey` combines shader paths, shader generations, and
  render-state fields from `PipelineDesc`; `InvalidateShaderPath(path)` drops
  affected cached leases and increments reload invalidation diagnostics.
- `RHI.PipelineManager` owns the promoted backend recompilation seam.
  `Recompile(handle, desc)` creates a replacement backend pipeline and stages it
  without changing the caller-held pool handle; `CommitPending()` promotes the
  staged replacement on the render thread and destroys the previous backend
  object.
- Failed recompiles keep the previously active backend pipeline alive.
  `PipelineManagerDiagnostics` reports create/recompile successes, failed
  recompiles, superseded pending reloads, committed reloads, live pipeline
  count, and pending reload count so last-known-good fallback is covered by
  CPU/null tests.

## Legacy RHI retirement decisions

`GRAPHICS-086` closes the remaining legacy RHI parity audit at
`CPUContracted` without adding new public RHI modules:

- Legacy `RHI.CommandUtils` one-shot Vulkan helpers are not retained as a public
  RHI convenience API. Promoted code records through `ICommandContext`,
  `FrameQueueSubmitPlanDesc`, and `ITransferQueue`; backend-local helpers stay
  inside `src/graphics/vulkan/`.
- Legacy `RHI.PersistentDescriptors` is replaced by promoted bindless,
  descriptor, and manager/lease contracts plus backend-local Vulkan descriptor
  pools. RHI callers do not hold native descriptor sets.
- Legacy `RHI.Swapchain`/`RHI.Image` ownership is split: RHI exposes
  backend-neutral texture descriptors, handles, present modes, and backbuffer
  handles, while Vulkan owns concrete swapchain/image state internally. RHI must
  stay platform-free; runtime supplies the platform-native window handle through
  `DeviceCreateDesc`.
- Legacy `RHI.SceneInstances` is replaced by the promoted `GpuInstanceData`
  record in `RHI.Types` plus renderer-owned `GpuWorld`/culling/material state.
  No separate scene-instance convenience module is retained.
- Legacy `RHI.CudaDevice` and `RHI.CudaError` are removed from the promoted
  default path. No current runtime, graphics, method, or benchmark consumer
  requires a CUDA compute seam; future CUDA work must open a new opt-in
  method/backend task with a concrete workload and verification plan.

## Module ABI hygiene (clang-20 / C++23 modules)

`RHI::ICommandContext` (`RHI.CommandContext.cppm`) is an **exported polymorphic
interface** consumed across many translation units — the renderer, the Null and
Vulkan backends, and every test `RecordingCommandContext` / `MockCommandContext`
double. Its vtable layout is part of the cross-TU ABI.

When you add, remove, or reorder any `virtual` member on `ICommandContext` (or
any other exported polymorphic RHI interface):

- **A clean preset rebuild is mandatory** — `rm -rf build/ci && cmake --preset
  ci && cmake --build --preset ci --target IntrinsicTests`. Do **not** trust an
  incremental build. Under clang-20's C++23-modules implementation, an
  incremental tree can retain a **stale module BMI** for a dependent TU that was
  not recompiled after the interface changed, so different TUs disagree on the
  vtable slot ordering. The result is a runtime SEGV (PC = 0x0) when a virtual
  is dispatched through a base `ICommandContext&` to the wrong/null slot — not a
  link error. This matches AGENTS.md §7: stale non-preset/incremental module
  trees are not valid verification for module changes.
- Prefer adding new non-pure virtuals **at the end** of the interface and keep
  bodies in the `.cpp` module implementation unit where the body is non-trivial
  (AGENTS.md §5), to minimise layout churn for existing slots.
- The interface's bodies live in `RHI.CommandContext.cpp` (HARDEN-073). The
  destructor is declared in the `.cppm` and defined there as the **out-of-line
  key function**, so the `ICommandContext` vtable is emitted in exactly one TU
  (a single authoritative module-owned emission). The defaulted-virtual bodies
  (`BindFrameSampledTexture`, `CopyTextureToBuffer`, `BindFrameSampledTextureAt`)
  are defined alongside it; `SubmitBarriers` is pure virtual and has no body to
  host. Note: this anchoring does **not** prevent the stale-BMI slot-mismatch
  failure mode above — that is a slot-offset problem, not a symbol-emission one;
  the clean-rebuild rule remains the authoritative prevention.
- `BindFrameSampledTextureAt(TextureHandle, std::uint32_t)` is the
  slot-explicit sibling of `BindFrameSampledTexture(TextureHandle)`. Backends
  that bridge framegraph sampled inputs through a global bindless array use it
  when two fullscreen passes in one command buffer must keep distinct sampled
  descriptors alive; backends with explicit descriptor binding may ignore it.
  The promoted Vulkan bridge reserves descriptor elements 0..5 for direct
  command-context updates: 0 default sampled input, 1 DebugView, 2 Present,
  3 SelectionOutline, and 4/5 object-space normal bake dilation output/scratch.
  Real bindless texture leases start after those bridge slots.

History: HARDEN-072 (`tasks/done/HARDEN-072-rhi-surface-fixes-for-default-recipe-pipeline-bringup.md`)
removed default arguments from `CopyTextureToBuffer` after they tripped a related
clang-20 vtable-mangling bug. BUG-013
(`tasks/done/BUG-013-backbuffer-readback-contract-vtable-segv.md`) was a
backbuffer-readback contract SEGV traced to stale BMIs after `BindFrameSampledTexture`
was added; it did **not** reproduce on a clean build — the contract suite is
green (225/225 in `IntrinsicGraphicsContractCpuTests`) once BMIs are consistent.

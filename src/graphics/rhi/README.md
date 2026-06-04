# Graphics/RHI

This directory contains the `RHI` module/files.

## Contents

- `CMakeLists.txt`
- `RHI.CommandContext.cppm`
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
  and later Vulkan queue recording consume this value contract without exposing
  API-native queue-family types through RHI.

## Timeline semaphores

- `RHI.TimelineSemaphore.cppm` declares `ITimelineSemaphore`, the backend-neutral
  signal/wait interface used by compiled cross-queue handoff records.
- The interface is intentionally minimal: `Signal(QueueAffinity, value)` records
  work completion on the producing queue, and `Wait(QueueAffinity, value)` gates
  the consuming queue. It does not expose Vulkan handles, queue-family indices,
  or backend submission ownership through RHI.
- The framegraph compiler owns deterministic value assignment and emits CPU-visible
  signal/wait/edge records. Concrete backend submission remains owned by later
  Vulkan multi-queue work.

## Transfer uploads

- `RHI.TransferQueue.cppm` declares `ITransferQueue`, the async upload seam used
  by runtime/streaming paths. Buffer uploads and single-subresource texture
  uploads remain non-blocking. `UploadTextureFullChain(TextureHandle,
  std::span<const std::byte>)` is the GRAPHICS-018T seam for packed full-chain
  texture uploads; callers must pack bytes according to
  `ComputeFullChainUploadLayout()` and backends fail closed with an invalid
  `TransferToken` when metadata or byte counts are unsupported.
- `RHI.TextureUpload.cppm` owns backend-neutral texture upload math:
  per-format byte/block helpers, Vulkan-safe subresource offset alignment, and
  the layer-major / mip-minor `TextureUploadLayout` used by future batched
  transfer submissions.

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
- `BindFrameSampledTextureAt(TextureHandle, std::uint32_t)` is the
  slot-explicit sibling of `BindFrameSampledTexture(TextureHandle)`. Backends
  that bridge framegraph sampled inputs through a global bindless array use it
  when two fullscreen passes in one command buffer must keep distinct sampled
  descriptors alive; backends with explicit descriptor binding may ignore it.

History: HARDEN-072 (`tasks/done/HARDEN-072-rhi-surface-fixes-for-default-recipe-pipeline-bringup.md`)
removed default arguments from `CopyTextureToBuffer` after they tripped a related
clang-20 vtable-mangling bug. BUG-013
(`tasks/done/BUG-013-backbuffer-readback-contract-vtable-segv.md`) was a
backbuffer-readback contract SEGV traced to stale BMIs after `BindFrameSampledTexture`
was added; it did **not** reproduce on a clean build — the contract suite is
green (225/225 in `IntrinsicGraphicsContractCpuTests`) once BMIs are consistent.

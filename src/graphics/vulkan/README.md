# Graphics/Backends/Vulkan

Promoted Vulkan 1.3 `IDevice` backend surface. Exports
`Extrinsic.Backends.Vulkan` with the `CreateVulkanDevice()` factory. The current
promoted lifecycle symbols are concrete and fail closed: until swapchain/device
bring-up is completed, `Initialize()` leaves the device non-operational and
`BeginFrame()` returns `false` instead of fabricating a frame. Full execution
requires a surface-capable physical device with `VK_KHR_timeline_semaphore`,
`VK_EXT_descriptor_indexing` (PARTIALLY_BOUND + UPDATE_AFTER_BIND), and dynamic
rendering (`VK_KHR_dynamic_rendering` / Vulkan 1.3 core).

## Frame lifecycle status

- `CreateVulkanDevice()` returns an `RHI::IDevice` instance without exposing
  concrete Vulkan types across renderer/RHI boundaries.
- Runtime selection is explicitly opt-in. `GraphicsBackend::Vulkan` continues to
  use the Null fallback unless `RenderConfig::EnablePromotedVulkanDevice` is true
  and the build enables `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` in a
  configuration that builds `ExtrinsicBackendsVulkan`.
- The promoted device lifecycle methods (`Initialize`, `Shutdown`, `WaitIdle`,
  `BeginFrame`, `EndFrame`, `Present`, `Resize`, backbuffer extent/handle access,
  present-mode selection, and graphics-context lookup) are defined in
  `Backends.Vulkan.Device.cpp`.
- `Initialize()` currently reports that promoted swapchain/device bring-up is not
  complete and keeps `IsOperational() == false`; renderers must continue to gate
  GPU command recording on `IDevice::IsOperational()`.
- Runtime now has a backend-neutral operational-transition seam for future
  Vulkan bring-up: when a device moves from non-operational to operational,
  runtime waits idle and calls `IRenderer::RebuildOperationalResources()` to
  rebuild renderer-owned material, `GpuWorld`, culling, and depth-prepass state
  through RHI managers. Vulkan still remains non-operational in this directory;
  no swapchain, surface, or presentation path is enabled by that seam.
- Non-operational instances still return valid service references for
  `GetBindlessHeap()` and `GetTransferQueue()`. These fail-closed fallbacks do
  not allocate GPU slots or upload data; they return invalid indices/tokens and
  make maintenance calls no-ops so callers never dereference null backend state.
  Fallback bindless allocation attempts increment
  `GetFallbackBindlessAllocationAttemptCount()`, fallback transfer-queue upload
  attempts (buffer or texture) increment
  `GetFallbackTransferUploadAttemptCount()`, fail-closed `CreatePipeline`
  calls increment `GetFallbackPipelineCreationAttemptCount()`, fail-closed
  `BeginFrame` calls (device non-operational or swapchain not yet brought up)
  increment `GetFallbackBeginFrameAttemptCount()`, and fail-closed `EndFrame`
  calls (device non-operational, taking the early-return path) increment
  `GetFallbackEndFrameAttemptCount()`. Each emits a logger breadcrumb for
  CPU-testable diagnostics. Counters are process-monotonic and never reset
  across `Initialize`/`Shutdown` cycles. The `BeginFrame`/`EndFrame` counter
  pair lets CPU diagnostics observe both halves of an unbalanced renderer
  frame loop driving a fail-closed Vulkan device, and CPU contract coverage
  asserts that paired Begin/End calls advance both counters in lockstep.
  `GetLastFallbackPipelineReason()` returns a structured
  `FallbackPipelineReason` enum (`None`, `PreBringUp`, `ShaderMissing`) so CPU
  diagnostics can distinguish "device or global pipeline layout not yet
  brought up" from "operational guard reached but shader/pipeline construction
  is still unimplemented". Bindless, transfer-queue, and frame-loop fallbacks
  intentionally do not yet expose a reason enum — each currently has a single
  fail-closed reason and the pattern is being piloted on `CreatePipeline` first.
  `GetFallbackDiagnosticsSnapshot()` returns a `FallbackDiagnosticsSnapshot`
  aggregate of all five counters plus the last pipeline reason in a single
  call, so CPU diagnostics consumers do not have to combine multiple
  independent free-function loads. Each field equals the corresponding
  individual accessor at the moment of capture; the aggregate read is not a
  tear-free transaction across fields, so concurrent fallback fires from
  another thread may land between two field loads (CPU contract tests run
  single-threaded so this is fine). Snapshot field load order is fixed:
  bindless, transfer, pipeline-count, last-pipeline-reason, begin-frame-count,
  end-frame-count.
- Buffer, texture, sampler, and pipeline `IDevice` overrides are symbol-complete
  in `Backends.Vulkan.Device.cpp`. They guard null/non-operational backend state.
  Texture creation now allocates VMA-backed `VkImage` objects and image views,
  and sampler creation creates real `VkSampler` objects once a future device
  bootstrap marks the backend operational; anisotropy remains disabled unless
  device feature negotiation records support. `WriteTexture()` now has a guarded
  synchronous staging-buffer upload path with mip/layer bounds checks, exact
  byte-size validation, sampled-usage validation, explicit depth-stencil upload
  rejection, and transfer-to-shader-read layout transitions that update tracked
  layout only after one-shot submission succeeds. Pipeline creation still returns
  invalid handles until shader/pipeline construction is wired.
- `Shutdown()` waits idle, flushes deferred deletes, and drains any still-live
  buffer, texture, sampler, and pipeline pool entries so partial bring-up slices
  do not leak backend resources when callers omit explicit per-resource destroys.
- Completing real Vulkan execution remains in `GRAPHICS-018`: create instance,
  surface, logical device, swapchain images, per-frame command buffers/sync,
  concrete resource creation/upload, pipeline creation, and presentation
  diagnostics before enabling opt-in `gpu;vulkan` smoke tests. The completed
  renderer reset seam removes one prior blocker, but Vulkan may not report
  operational until fallback bindless/transfer behavior and real backend
  resources are reconciled behind the same RHI interfaces.
- Renderer/RHI behavior that is not Vulkan-specific is documented canonically in
  [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md).

## Module surface

| Module | Exported API |
|---|---|
| `Extrinsic.Backends.Vulkan` | `CreateVulkanDevice()`, `GetFallbackBindlessAllocationAttemptCount()`, `GetFallbackTransferUploadAttemptCount()`, `GetFallbackPipelineCreationAttemptCount()`, `GetFallbackBeginFrameAttemptCount()`, `GetFallbackEndFrameAttemptCount()`, `GetLastFallbackPipelineReason()`, `FallbackPipelineReason`, `GetFallbackDiagnosticsSnapshot()`, `FallbackDiagnosticsSnapshot` |
| `Extrinsic.Backends.Vulkan:{Device,Queues,Memory,CommandPools,Descriptors,Swapchain,Pipelines,Transfer,Sync,Surface,Diagnostics}` | *(internal partitions — not re-exported)* |

## File inventory

| File | Responsibility |
|---|---|
| `Backends.Vulkan.cppm` | Umbrella interface — exports `CreateVulkanDevice()` and fail-closed fallback diagnostics. |
| `Backends.Vulkan.Device.cppm` | Non-re-exported `:Device` partition — `VulkanDevice` declaration and aggregate backend ownership. |
| `Backends.Vulkan.Queues.cppm` | Non-re-exported `:Queues` partition — queue-family and raw queue state contracts. |
| `Backends.Vulkan.Memory.cppm` | Non-re-exported `:Memory` partition — backend buffer/image/sampler records. |
| `Backends.Vulkan.CommandPools.cppm` | Non-re-exported `:CommandPools` partition — command-context declaration. |
| `Backends.Vulkan.Descriptors.cppm` | Non-re-exported `:Descriptors` partition — bindless descriptor heap declaration. |
| `Backends.Vulkan.Swapchain.cppm` | Non-re-exported `:Swapchain` partition — swapchain state contract. |
| `Backends.Vulkan.Pipelines.cppm` | Non-re-exported `:Pipelines` partition — pipeline record and RHI-to-Vulkan mapping declarations. |
| `Backends.Vulkan.Transfer.cppm` | Non-re-exported `:Transfer` partition — staging belt and transfer queue declarations. |
| `Backends.Vulkan.Sync.cppm` | Non-re-exported `:Sync` partition — frames-in-flight, per-frame sync, deferred deletion. |
| `Backends.Vulkan.Surface.cppm` | Non-re-exported `:Surface` partition — surface ownership contract. |
| `Backends.Vulkan.Diagnostics.cppm` | Non-re-exported `:Diagnostics` partition — profiler/timestamp declaration. |
| `Backends.Vulkan.Internal.cppm` | Retired migration stub; contains no module declaration. |
| `Backends.Vulkan.Mappings.cpp` | §2 RHI enum → Vulkan enum conversion tables (`ToVkFormat`, `ToVkImageLayout`, `AspectFromFormat`, `ToVkBufferUsage`, etc.) |
| `Backends.Vulkan.DiagnosticsLogging.cpp` | §3 logging bridge for `VK_CHECK_*` macros declared in `Vulkan.hpp`; routes diagnostics through `Core::Log::*` without importing modules from the header. |
| `Backends.Vulkan.Staging.cpp` | §5 `StagingBelt` — host-visible ring-buffer for async uploads |
| `Backends.Vulkan.Profiler.cpp` | §6 `VulkanProfiler` — `IProfiler` backed by `VkQueryPool` timestamps |
| `Backends.Vulkan.Bindless.cpp` | §7 `VulkanBindlessHeap` — `IBindlessHeap` with `PARTIALLY_BOUND` descriptor array |
| `Backends.Vulkan.Transfer.cpp` | §8 `VulkanTransferQueue` — `ITransferQueue` via timeline semaphore + `StagingBelt` |
| `Backends.Vulkan.CommandContext.cpp` | §9 `VulkanCommandContext` — `ICommandContext` (one per frame-in-flight slot) |
| `Backends.Vulkan.Device.cpp` | §11 `VulkanDevice` implementations + §12 `CreateVulkanDevice()` factory |
| `Backends.Vulkan.cpp` | *(empty placeholder — kept to avoid CMake source-list churn)* |
| `Vma.cpp` | VMA implementation TU (`VMA_IMPLEMENTATION` guard) — compiles without modules |
| `VmaConfig.hpp` | VMA configuration header |
| `Vulkan.hpp` | Vulkan/volk/VMA include aggregator |

## Partition design

Internal Vulkan types are declared in focused non-re-exported module partitions.
Implementation `.cpp` files import the partition that owns the declarations they
define or consume. For example:

```cpp
module;
#include "Vulkan.hpp"   // provides VkDevice, VmaAllocator, etc.
module Extrinsic.Backends.Vulkan;
import :Transfer;       // gets StagingBelt/VulkanTransferQueue declarations
```

The umbrella `Backends.Vulkan.cppm` does **not** `export import` any internal
partition, so external consumers of `Extrinsic.Backends.Vulkan` see only
`CreateVulkanDevice()` plus fail-closed fallback diagnostics — concrete Vulkan
types remain hidden.

## Diagnostics

Vulkan backend implementation files use `Core::Log::*` for warnings/errors and
must not write directly to `stderr`. This keeps fail-closed backend diagnostics
visible through the same logging surface as renderer/RHI contract checks. The
`VK_CHECK_*` macros in `Vulkan.hpp` route through `ReportVkCheckFailure()` in
`Backends.Vulkan.DiagnosticsLogging.cpp` so the header remains module-import-free
while still using the project logger.

## Dependencies

```
Extrinsic.Backends.Vulkan
  → ExtrinsicRHI     (public)
  → ExtrinsicCore    (private)
  → Vulkan::Vulkan   (private)
  → volk             (private)
  → VulkanMemoryAllocator (private)
  → glfw             (private)
```


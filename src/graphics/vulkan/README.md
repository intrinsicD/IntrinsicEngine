# Graphics/Backends/Vulkan

Promoted Vulkan 1.3 `IDevice` backend surface. Exports
`Extrinsic.Backends.Vulkan` with the `CreateVulkanDevice()` factory. The current
promoted lifecycle symbols are concrete and fail closed: with a native GLFW
window, `Initialize()` now performs guarded Vulkan bootstrap by initializing
volk, creating a `VkInstance`, creating a window surface, probing for a
surface-capable physical device/queue-family/swapchain-support tuple that also
supports required Vulkan 1.2/1.3 features (`timelineSemaphore`, descriptor
indexing update-after-bind/partially-bound, `bufferDeviceAddress`, and dynamic
rendering), creating a logical device with those features and `VK_KHR_swapchain`,
loading device-level volk entry points, acquiring graphics/present/transfer `VkQueue` handles,
creating a VMA allocator, allocating per-frame command pools, primary command
buffers, fences, and acquire/render semaphores, then creating a guarded swapchain
  with image views and backend-local `RHI::TextureHandle` registrations for the
  swapchain images, live internal bindless/global-pipeline-layout/transfer service
  objects, rebound command contexts, and a global layout capable of the RHI
  maximum push-constant range. It still leaves the device non-operational until
  canonical renderer pass command recording, synchronization/barrier validation,
  queue-family ownership handling where needed, and public service fallback reconciliation land; guarded direct `BeginFrame()` can acquire only after service-ready bootstrap and otherwise returns `false`
instead of fabricating a frame. Full execution requires a surface-capable
physical device with timeline semaphores, descriptor indexing
(PARTIALLY_BOUND + UPDATE_AFTER_BIND for sampled images), buffer device
addresses, and dynamic rendering
available through the Vulkan 1.2/1.3 feature chain.

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
- `Initialize()` performs only the guarded bootstrap/probe plus logical-device,
  queue, allocator, per-frame command/sync acquisition, and swapchain image/view/handle registration listed above. It keeps `IsOperational() == false`;
  renderers must continue to gate GPU command
  recording on `IDevice::IsOperational()`. If the supplied window has no native
  GLFW handle (for example the default CPU/null path), bootstrap is skipped
  without creating Vulkan handles.
- `GetVulkanBootstrapDiagnosticsSnapshot()` reports the most recent bootstrap
  attempt as backend-specific CPU diagnostics: status, last Vulkan result code,
  whether a native window/volk/validation/instance/surface/device probe was
  reached, queue-family indices, swapchain extension/surface support, logical
  device creation, graphics/present/transfer queue acquisition, VMA allocator
  creation, per-frame command-pool/command-buffer/fence/semaphore counts, and
  swapchain creation/image enumeration/image-view/handle-registration counts and extent.
  It also reports whether required device features for later operational paths
  were supported and enabled: descriptor indexing, timeline semaphores, and
  dynamic rendering, plus buffer device addresses for BDA-only promoted buffer
  paths. Devices lacking those required features are skipped during
  physical-device selection so later bindless/transfer/pipeline slices do not
  accidentally build on an unsuitable adapter.
  The snapshot avoids Vulkan-native types and is not an RHI/renderer branching seam;
  renderer/runtime code must continue to use `IDevice::IsOperational()`.
- Runtime now has a backend-neutral operational-transition seam for future
  Vulkan bring-up: when a device moves from non-operational to operational,
  runtime waits idle and calls `IRenderer::RebuildOperationalResources()` to
  rebuild renderer-owned material, `GpuWorld`, culling, and depth-prepass state
  through RHI managers. Vulkan still remains non-operational in this directory;
  no presentation or operational frame path is enabled by that seam.
- `GetVulkanServiceDiagnosticsSnapshot()` reports guarded post-bootstrap service
  handoff: bindless heap creation, global pipeline-layout creation, transfer
  queue/staging creation, command-context rebinding, bindless capacity, clean
  failure/skipped statuses, and the backend-owned operational predicate inputs.
  Constructors for these internal service objects leave invalid state and log
  diagnostics instead of aborting when Vulkan allocation fails, so the promoted
  backend can remain fail-closed. Public `GetBindlessHeap()` and
  `GetTransferQueue()` now route through the same predicate as
  `IDevice::IsOperational()`: live logical-device/swapchain/per-frame/service
  state must be present, and operational safety blockers for canonical renderer
  resource/descriptor/pass execution must be cleared. The guarded
  bootstrap currently satisfies the live-prerequisite half only, so the public
  accessors still return fail-closed fallback services even when the service
  snapshot reports `Ready`.
- The internal `VulkanTransferQueue` path is hardened for future public handoff:
  command-buffer allocation/begin/end/submit and semaphore-query failures now log
  diagnostics and return invalid `RHI::TransferToken` values instead of aborting
  through `VK_CHECK_FATAL`. Uploads also preflight service validity, data
  pointers, sizes, buffer ranges, texture mip/layer bounds, transfer-dst usage,
  and staging allocation before recording commands. Public transfer access still
  resolves to the fallback queue while non-operational, even when the internal
  service is `Ready`.
- `VulkanDevice::CreatePipeline()` now has a guarded concrete Vulkan path once
  bootstrap has produced a logical device and global pipeline layout. It reads
  SPIR-V shader files from `RHI::PipelineDesc`, creates shader modules, and builds
  compute pipelines or dynamic-rendering graphics pipelines, including depth-only
  graphics pipelines, with BDA-only vertex input, dynamic viewport/scissor state, RHI raster/depth/blend mappings, and the
  global bindless layout. `GetVulkanPipelineDiagnosticsSnapshot()` reports
  pre-bring-up skips, invalid descriptions, shader-read failures, shader-module
  failures, Vulkan pipeline-creation failures, and successful graphics/compute
  creation counts without exposing `Vk*` handles. Renderer and RHI manager code
  still gates on `IDevice::IsOperational()`, so this direct backend resource path
  is an opt-in bootstrap prerequisite and does not make canonical frame execution
  operational.
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
  `GetFallbackEndFrameAttemptCount()`. Fail-closed `Present` calls (device or
  swapchain not yet operational, taking the early-return path) increment
  `GetFallbackPresentAttemptCount()`. Fail-closed `Resize` calls (device or
  swapchain not yet operational) increment `GetFallbackResizeAttemptCount()`
  while still recording the requested extent for CPU/runtime diagnostics. Each
  emits a logger breadcrumb for CPU-testable diagnostics. Counters are
  process-monotonic and never reset across `Initialize`/`Shutdown` cycles. The
  `BeginFrame`/`EndFrame` counter
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
  aggregate of all seven counters plus the last pipeline reason in a single
  call, so CPU diagnostics consumers do not have to combine multiple
  independent free-function loads. Each field equals the corresponding
  individual accessor at the moment of capture; the aggregate read is not a
  tear-free transaction across fields, so concurrent fallback fires from
  another thread may land between two field loads (CPU contract tests run
  single-threaded so this is fine). Snapshot field load order is fixed:
  bindless, transfer, pipeline-count, last-pipeline-reason, begin-frame-count,
  end-frame-count, present-count, resize-count.
- `GetVulkanFrameLifecycleDiagnosticsSnapshot()` reports the most recent
  promoted Vulkan frame lifecycle attempt with a backend-local structured status
  taxonomy for `BeginFrame`, `EndFrame`, `Present`, and `Resize`. Fail-closed
  paths populate `SkippedNotOperational`, `SkippedNoSwapchain`,
  `SkippedNoSwapchainImages`, and pending resize states. After guarded bootstrap
  has produced a logical device, swapchain, per-frame command/sync resources,
  and live internal services, direct opt-in smoke coverage can also exercise an
  empty `vkAcquireNextImageKHR` -> command-buffer submit -> `vkQueuePresentKHR`
  path while `IsOperational()` remains false; those calls populate `Acquired`,
  `Submitted`, `Presented`, `Suboptimal`, `OutOfDate`, and failure variants.
  `Resize()` now records zero-sized requests as pending recreation, can recreate
  the swapchain with safe idle synchronization and old image-view/handle
  retirement when a nonzero extent is available, and reports `Recreated` or
  `FailedRecreate`. Device-lost results from acquire, submit, present, recreate,
  one-shot uploads, and resource/pipeline creation move the backend back to a
  fail-closed state and surface `DeviceLost` in lifecycle diagnostics. The
  snapshot also carries the last frame/image indices, requested resize extent,
  availability booleans, pending-resize/device-lost flags, last Vulkan result
  code, and the same process-monotonic lifecycle counters exposed by
  `FallbackDiagnosticsSnapshot`. The snapshot is backend-specific diagnostics
  only; it does not expose Vulkan-native types and must not become a renderer/RHI
  branching seam.
- `VulkanCommandContext` is fail-closed before operational bring-up: unbound or
  not-begun command recording calls skip with logger diagnostics instead of
  issuing Vulkan commands against null/non-recording command-buffer state.
  `GetFallbackCommandRecordingAttemptCount()` is a process-monotonic backend
  diagnostic counter for those skips. It is not part of renderer branching;
  renderer/runtime code must still gate command recording on `IDevice::IsOperational()`.
- Buffer, texture, sampler, and pipeline `IDevice` overrides are symbol-complete
  in `Backends.Vulkan.Device.cpp`. They guard null/non-live backend state and can
  be exercised directly after service-ready bootstrap while `IsOperational()`
  remains false; RHI managers and renderer code still cannot reach these live
  paths until the backend-neutral operational predicate is cleared. Buffer
  creation uses VMA with buffer-device-address allocator support and can back
  `RHI::GpuSceneTable`, culling draw-bucket, material, geometry, transform,
  bounds, and light SSBO patterns. Texture creation allocates VMA-backed
  `VkImage` objects and image views for sampled textures plus depth/color dynamic
  rendering attachments, and sampler creation creates real `VkSampler` objects
  with backend-local translation from `RHI::SamplerDesc::BorderColor` to Vulkan
  `VkBorderColor`; anisotropy remains disabled unless device feature negotiation
  records support. The live internal `VulkanBindlessHeap` now resolves
  backend-owned RHI texture/sampler handles into queued descriptor writes through
  a Vulkan-local resolver set by `VulkanDevice`, so future public service handoff
  does not require Vulkan handles or live ECS knowledge in renderer/RHI callers.
  `WriteTexture()` now has a guarded
  synchronous staging-buffer upload path with mip/layer bounds checks, exact
  byte-size validation, sampled- and transfer-dst-usage validation, explicit
  depth-stencil upload rejection, and transfer-to-shader-read layout transitions that update tracked
  layout only after one-shot submission succeeds. Pipeline creation now builds
  SPIR-V-backed compute or dynamic-rendering graphics pipelines once guarded
  bootstrap has created the Vulkan device/global layout; `assets/shaders/depth_prepass.vert`
  is the canonical depth-prepass shader source used by opt-in smoke coverage.
  Pre-bring-up and invalid shader/description paths remain fail-closed with diagnostics.
- `Shutdown()` waits idle, flushes deferred deletes, drains any still-live buffer,
  texture, sampler, and pipeline pool entries, destroys swapchain/global objects,
  destroys per-frame command/sync resources, then tears down VMA/device/surface/
  instance state so partial bring-up slices do not leak backend resources when
  callers omit explicit per-resource destroys.
- Completing real Vulkan execution remains in `GRAPHICS-018`: instance/surface/
  physical-device probing with required Vulkan 1.2/1.3 feature negotiation,
  logical-device/queue/allocator/per-frame resource acquisition, and guarded
  swapchain image/view/handle registration are present,
  plus guarded live bindless/global-layout/transfer service handoff, hardened
  internal transfer invalid-token failure behavior, and nonfatal command-context
  recording skips, concrete SPIR-V pipeline creation, and a guarded direct
  empty-frame acquire/submit/present smoke path, guarded resource/descriptor
  readiness, and depth-only graphics pipeline smoke coverage are present, but
  canonical renderer command recording/pass execution, synchronization/barrier
  validation, queue-family ownership handling where needed, and public service
  fallback reconciliation still need to land before
  `IsOperational()` can become true. The opt-in `VulkanBootstrapSmoke` test is
  labeled `gpu;vulkan` and verifies that bootstrap either creates swapchain
  image/view/handle state or fails/skips cleanly on unsupported hosts. When
  service-ready bootstrap succeeds, the smoke test records an empty command
  buffer that transitions the acquired backbuffer to present layout, submits it,
  and presents it while asserting that `IsOperational()` remains false and public
  bindless/transfer access still returns fail-closed fallbacks. The completed
  renderer reset seam removes one
  prior blocker, but Vulkan may not report operational until fallback
  bindless/transfer behavior and real backend pass execution are reconciled behind
  the same RHI interfaces.
- Renderer/RHI behavior that is not Vulkan-specific is documented canonically in
  [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md).

## Module surface

| Module | Exported API |
|---|---|
| `Extrinsic.Backends.Vulkan` | `CreateVulkanDevice()`, `GetVulkanBootstrapDiagnosticsSnapshot()`, `VulkanBootstrapStatus`, `VulkanBootstrapDiagnosticsSnapshot`, `GetVulkanFrameLifecycleDiagnosticsSnapshot()`, `VulkanFrameBeginStatus`, `VulkanFrameEndStatus`, `VulkanFramePresentStatus`, `VulkanFrameResizeStatus`, `VulkanFrameLifecycleDiagnosticsSnapshot`, `GetVulkanServiceDiagnosticsSnapshot()`, `VulkanServiceBootstrapStatus`, `VulkanServiceDiagnosticsSnapshot`, `GetVulkanPipelineDiagnosticsSnapshot()`, `VulkanPipelineCreationStatus`, `VulkanPipelineDiagnosticsSnapshot`, `GetFallbackBindlessAllocationAttemptCount()`, `GetFallbackTransferUploadAttemptCount()`, `GetFallbackPipelineCreationAttemptCount()`, `GetFallbackBeginFrameAttemptCount()`, `GetFallbackEndFrameAttemptCount()`, `GetFallbackPresentAttemptCount()`, `GetFallbackResizeAttemptCount()`, `GetFallbackCommandRecordingAttemptCount()`, `GetLastFallbackPipelineReason()`, `FallbackPipelineReason`, `GetFallbackDiagnosticsSnapshot()`, `FallbackDiagnosticsSnapshot` |
| `Extrinsic.Backends.Vulkan:{Device,Queues,Memory,CommandPools,Descriptors,Swapchain,Pipelines,Transfer,Sync,Surface,Diagnostics}` | *(internal partitions — not re-exported)* |

## File inventory

| File | Responsibility |
|---|---|
| `Backends.Vulkan.cppm` | Umbrella interface — exports `CreateVulkanDevice()`, bootstrap diagnostics, and fail-closed fallback diagnostics. |
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
| `Backends.Vulkan.CommandContext.cpp` | §9 `VulkanCommandContext` — `ICommandContext` (one per frame-in-flight slot), with fail-closed unbound/non-recording command skips and diagnostics. |
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


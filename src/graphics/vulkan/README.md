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
  backend can remain fail-closed. Public `GetBindlessHeap()` still routes
  through `IDevice::IsOperational()` so descriptor allocation remains blocked
  until canonical renderer resource/descriptor/pass execution is reconciled.
  Public `GetTransferQueue()` now exposes the live async upload service once
  guarded live prerequisites are ready (`PublicTransferQueueExposed = true`),
  while the device itself and bindless heap remain non-operational/fail-closed.
- The internal `VulkanTransferQueue` path is hardened for future public handoff:
  command-buffer allocation/begin/end/submit and semaphore-query failures now log
  diagnostics and return invalid `RHI::TransferToken` values instead of aborting
  through `VK_CHECK_FATAL`. Uploads also preflight service validity, data
  pointers, sizes, buffer ranges, texture mip/layer bounds, transfer-dst usage,
  and staging allocation before recording commands. Submitted command buffers are
  retained until the timeline semaphore reports completion, then reclaimed by
  `CollectCompleted()` with the staging-belt allocation.
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
- Pre-bootstrap and non-service-ready instances still return valid fail-closed
  service references for `GetBindlessHeap()` and `GetTransferQueue()`. These
  fallbacks do not allocate GPU slots or upload data; they return invalid
  indices/tokens and make maintenance calls no-ops so callers never dereference
  null backend state. Once guarded live prerequisites are ready,
  `GetTransferQueue()` returns the live Vulkan transfer queue even while
  `IsOperational()` remains false; `GetBindlessHeap()` continues to return the
  fallback heap until operational promotion.
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
  while still recording the requested extent for CPU/runtime diagnostics. Counters
  are process-monotonic and never reset across `Initialize`/`Shutdown` cycles.
  Frame-loop fail-closed logs (`BeginFrame`, `EndFrame`, `Present`) are emitted
  only on the first fire to prevent log spam at 60 Hz; counters continue to
  increment on every call so diagnostic consumers still observe the full
  sequence. Resize fail-closed logs are not rate-limited because window-resize
  events are inherently low-frequency. Per `GRAPHICS-018Q`, per-call warn
  breadcrumbs on the bindless / transfer-queue / pipeline-creation fallback
  paths remain canonical for now because those callsites fire infrequently
  while non-operational and the visibility helps catch accidental loops or
  missing operational gating before bring-up; migration of any per-call
  counter to once-per-frame rate-limited breadcrumbs (with a cumulative-
  skipped count appended to the next emission) is a separate semantic task
  scoped to that counter only when operational bring-up demonstrates many-per-
  frame fallback firing, never a sweeping retrofit, and must keep the
  underlying counter exact. The `BeginFrame`/`EndFrame` counter pair
  lets CPU diagnostics observe both halves of an unbalanced renderer frame loop
  driving a fail-closed Vulkan device, and CPU contract coverage asserts that
  paired Begin/End calls advance both counters in lockstep.
  `GetLastFallbackPipelineReason()` returns a structured
  `FallbackPipelineReason` enum (`None`, `PreBringUp`, `ShaderMissing`) so CPU
  diagnostics can distinguish "device or global pipeline layout not yet
  brought up" from "operational guard reached but shader/pipeline construction
  is still unimplemented". Bindless, transfer-queue, and frame-loop fallbacks
  intentionally do not yet expose a reason enum — each currently has a single
  fail-closed reason and the pattern is being piloted on `CreatePipeline` first.
  Per `GRAPHICS-018Q`, this 1:1 path-scoping is canonical: future device-loss /
  extension / feature-negotiation reasons in the pipeline path are appended to
  the existing `FallbackPipelineReason` enum, and any second reason in another
  counter introduces a *new* path-local `FallbackXxxReason` enum named after
  that counter (e.g. `FallbackBindlessReason`, `FallbackTransferUploadReason`,
  `FallbackBeginFrameReason`) with a matching `LastXxxReason` field appended
  to `FallbackDiagnosticsSnapshot` after the existing eight fields, never a
  combined mega-enum.
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
  taxonomy for `BeginFrame`, `EndFrame`, `Present`, and `Resize`. All enum
  variants are populated from the real lifecycle paths: fail-closed paths emit
  `SkippedNotOperational`, `SkippedNoSwapchain`, and `SkippedNoSwapchainImages`
  for pre-operational frames, and `RecordedPendingNotOperational`,
  `RecordedPendingNoSwapchain`, `RecordedPendingRecreate`, `Recreated`, and
  `FailedRecreate` for resize operations. Guarded direct paths (reachable after
  service-ready bootstrap) populate `Acquired`, `Suboptimal`, `OutOfDate`,
  `FailedAcquire` for `BeginFrame`; `Submitted`, `FailedSubmit` for `EndFrame`;
  `Presented`, `Suboptimal`, `OutOfDate`, `FailedPresent` for `Present`. Device-
  lost results from acquire, submit, present, recreate, one-shot uploads, and
  resource/pipeline creation paths set `m_DeviceLost`, surface `DeviceLost` in
  the snapshot, and route subsequent lifecycle calls back to fail-closed
  `SkippedNotOperational` paths via `NoteDeviceLostIfNeeded`. The snapshot also
  carries the last frame/image indices, requested resize extent, availability
  booleans, pending-resize/device-lost flags, last Vulkan result code, and the
  same process-monotonic lifecycle counters exposed by `FallbackDiagnosticsSnapshot`.
  The snapshot is backend-specific diagnostics only; it does not expose
  Vulkan-native types and must not become a renderer/RHI branching seam.
  CPU contract coverage in `Test.VulkanFailClosedContract.cpp`
  (`FailClosedLifecyclePathsNeverSetDeviceLost`,
  `DeviceLostFlagStaysFalseAcrossInitializeShutdownCyclesOnNullWindow`)
  asserts that fail-closed lifecycle skip paths never raise `DeviceLost` and
  that `LastVkResult` stays `0` (no Vulkan call issued) so renderer/runtime CI
  cannot accidentally confuse "Vulkan never bootstrapped" with "Vulkan was lost".
- `VulkanCommandContext` is fail-closed before operational bring-up: unbound or
  not-begun command recording calls skip with logger diagnostics instead of
  issuing Vulkan commands against null/non-recording command-buffer state.
  `GetFallbackCommandRecordingAttemptCount()` is a process-monotonic backend
  diagnostic counter for those skips. It is not part of renderer branching;
  renderer/runtime code must still gate command recording on `IDevice::IsOperational()`.
  After service-ready bootstrap, `Bind()` additionally records the resolved
  graphics/present/transfer queue family indices and rebinds the live frame
  command buffer plus buffer/image/pipeline pools so per-frame `BeginFrame`
  routes the canonical render-graph executor through the live command buffer
  while the renderer brackets the entire executor invocation with one
  `Begin()`/`End()` pair. `TextureBarrier`/`SubmitBarriers` translate
  `RHI::TextureLayout`/`RHI::MemoryAccess` into `vkCmdPipelineBarrier2` records
  and update each touched `VulkanImage::CurrentLayout` so subsequent
  uploads/barriers in the same frame observe the most recent recorded layout.
- `VulkanDevice::CreateBuffer` and `VulkanDevice::CreateTexture` declare
  `VK_SHARING_MODE_CONCURRENT` with the unique graphics/transfer queue-family
  list whenever the transfer queue lives on a different family from graphics,
  mirroring the existing swapchain handling for differing graphics/present
  families. This lets transfer-queue uploads and graphics-queue reads touch
  the same resource without explicit queue-family ownership-transfer barrier
  pairs while operational pass execution is being landed.
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
  records support. Per `GRAPHICS-018Q`, anisotropy stays expressed through
  `RHI::SamplerDesc::MaxAnisotropy` (no new RHI enum or cap query): the backend
  probes `VkPhysicalDeviceFeatures::samplerAnisotropy` during physical-device
  selection alongside the existing required Vulkan 1.2 / 1.3 features, enables
  it on the logical device when supported, records support / enablement on
  `GetVulkanBootstrapDiagnosticsSnapshot()`, and at sampler creation silently
  disables anisotropy when the feature is unsupported or `MaxAnisotropy <= 1.0`;
  otherwise it clamps the requested value to
  `min(MaxAnisotropy, VkPhysicalDeviceLimits::maxSamplerAnisotropy)` and emits
  one warn breadcrumb when clamping reduces the value. Missing support never
  fails sampler creation. The live internal `VulkanBindlessHeap` now resolves
  backend-owned RHI texture/sampler handles into queued descriptor writes through
  a Vulkan-local resolver set by `VulkanDevice`, so future public service handoff
  does not require Vulkan handles or live ECS knowledge in renderer/RHI callers.
  `WriteTexture()` now has a guarded
  synchronous staging-buffer upload path with mip/layer bounds checks, exact
  byte-size validation, sampled- and transfer-dst-usage validation, explicit
  depth-stencil upload rejection, and transfer-to-shader-read layout transitions that update tracked
  layout only after one-shot submission succeeds. Per `GRAPHICS-018Q`, this
  guarded synchronous one-subresource path stays the fail-closed correctness
  baseline; runtime/streaming uploads must use `RHI::ITransferQueue` (the
  canonical seam declared by `GRAPHICS-026`) rather than the blocking
  graphics-queue helper, per-subresource layout tracking stays whole-image
  until multi-subresource batching lands, and multi-mip / multi-layer /
  cubemap batching plus opt-in `gpu;vulkan` smoke for those uploads is owned
  by [`GRAPHICS-018T`](../../../tasks/done/GRAPHICS-018T-texture-upload-batching.md),
  not by 018Q. Per `GRAPHICS-018T` slice A.1, the CPU-testable
  per-subresource byte-size and packed full-mip-chain offset math is
  exposed as backend-neutral free functions in
  `Extrinsic.RHI.TextureUpload` (`BytesPerBlock(Format)`,
  `IsBlockCompressedFormat(Format)`, `IsDepthStencilFormat(Format)`,
  `MipExtent(extent, mipLevel)`,
  `ComputeSubresourceUploadSize(TextureDesc, mipLevel)`, and
  `ComputeFullChainUploadLayout(TextureDesc) ->
  Core::Expected<TextureUploadLayout>`), with the canonical layer-major
  / mip-minor packing convention plus deterministic
  `InvalidArgument`/`InvalidFormat` rejection for zero extents and
  depth-stencil/`Undefined` formats. Slice A.2a adds the RHI seam
  `ITransferQueue::UploadTextureFullChain(TextureHandle,
  std::span<const std::byte>)` for callers that provide this packed
  full-chain layout. The Null backend and non-operational Vulkan fallback
  return invalid tokens for that seam. The live Vulkan transfer queue now
  validates 2D color texture-array / six-face cubemap metadata and exact byte counts, copies
  initialized subresource ranges into one staging-belt allocation, emits one
  whole-image `Undefined`/current-layout → `TransferDst` barrier, one
  `vkCmdCopyBufferToImage` with a `VkBufferImageCopy` array built from
  `TextureUploadLayout`, and one `TransferDst` → `ShaderReadOnly` barrier
  before returning the timeline `TransferToken`. Slice A.2c migrates the retained
  colormap LUT initialization caller to `IDevice::GetTransferQueue().UploadTexture()`
  and gates bindless lookup on `ColormapSystem::IsReady()` so first-frame draws
  can skip while the asynchronous upload token is pending. Slice A.2d exposes
  the live transfer queue once guarded live prerequisites are ready and adds an
  opt-in `gpu;vulkan` smoke for a 2D multi-mip full-chain upload. Slice B extends
  the same batched transfer path to six-face cubemaps and expands the opt-in smoke
  to cover 2D array and cubemap full-chain uploads. 3D batching remains deferred;
  the existing single-subresource `WriteTexture()` /
  one-`UploadTexture()` paths remain the fail-closed correctness baseline.
  Pipeline creation now builds
  SPIR-V-backed compute or dynamic-rendering graphics pipelines once guarded
  bootstrap has created the Vulkan device/global layout; `assets/shaders/depth_prepass.vert`
  is the canonical depth-prepass shader source used by opt-in smoke coverage.
  Pre-bring-up and invalid shader/description paths remain fail-closed with diagnostics.
- `Shutdown()` waits idle, flushes deferred deletes, drains any still-live buffer,
  texture, sampler, and pipeline pool entries, destroys swapchain/global objects,
  destroys per-frame command/sync resources, then tears down VMA/device/surface/
  instance state so partial bring-up slices do not leak backend resources when
  callers omit explicit per-resource destroys.
- `GRAPHICS-018` brought up all major guarded Vulkan paths: instance/surface/
  physical-device probing with required Vulkan 1.2/1.3 feature negotiation,
  logical-device/queue/allocator/per-frame resource acquisition, swapchain
  image/view/handle registration, live internal bindless/global-layout/transfer
  service handoff, hardened internal transfer failure behavior, nonfatal
  command-context recording skips, concrete SPIR-V pipeline creation, guarded
  direct acquire/submit/present, resource/descriptor readiness, swapchain
  recreation with device-loss transitions, and structured lifecycle diagnostics
  for all lifecycle/resize/device-loss paths. Render-graph bracketing wraps the
  full executor invocation with a single `VulkanCommandContext::Begin()`/`End()`
  pair, soft-skipped passes report structured `RenderGraphCommandPassStats`
  entries, recorded barriers translate through `vkCmdPipelineBarrier2`, and
  buffer/texture sharing mode handles graphics/transfer queue-family separation.
  The opt-in `VulkanBootstrapSmoke` test is labeled `gpu;vulkan` and verifies
  bootstrap state plus the guarded acquire/command-record/submit/present path
  while asserting that `IsOperational()` remains false and public
  bindless/transfer access still returns fail-closed fallbacks. Remaining before
  `IsOperational()` can become true: canonical renderer pass command execution
  beyond `CullingPass`/`DepthPrepass` routing and public service fallback
  reconciliation.
- Renderer/RHI behavior that is not Vulkan-specific is documented canonically in
  [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md).

## Operational readiness gate (GRAPHICS-033)

The promoted Vulkan backend remains fail-closed until the GRAPHICS-033
operational gate is implemented and satisfied. `RHI::IDevice::IsOperational()`
must become `true` only after one Vulkan-owned single-source evaluator reports an
operational status; runtime, renderer, and app code must not re-derive this from
CMake options, config flags, bootstrap snapshots, or fallback counters.

Implementation child A landed the exported diagnostic seam
(`GRAPHICS-033A`):
`EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) ->
VulkanOperationalStatus` is now exported from `Extrinsic.Backends.Vulkan`
(declared in `Backends.Vulkan.cppm`, implemented in
`Backends.Vulkan.OperationalStatus.cpp`). The inputs are backend-public,
non-native booleans / reason bits (no `Vk*` handles). `VulkanDevice::IsOperational()`
consumes the evaluator through `VulkanDevice::ComputeOperationalPredicate()`;
runtime startup reconciliation (`GRAPHICS-033B`) consumes the same result.
Implementation child C landed the minimal-recipe recording bodies
(`GRAPHICS-033C`): `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug`
recording bodies execute against the live command context once the device is
operational, the slot-0 default-debug-surface pipeline lease is rebuilt
byte-identical by `IRenderer::RebuildOperationalResources()` on the
false→true transition, and the executor lambda routes the live graphics
`VulkanCommandContext` to the minimal-recipe pass routes. `BuildOperationalInputs()`
now reports `MinimalRecipeRecordingPresent = true` because the recording
bodies are a codebase fact.

Implementation child E landed the gate-7 wiring (`GRAPHICS-033E`):
`RHI::IDevice::NoteRecipeGraphValidation(bool)` is a backend-neutral CPU-public
setter (default no-op for non-Vulkan backends). After every recipe compile
attempt the renderer calls `ValidateRecipeCompiledGraph(...)` against the just-
compiled graph and publishes
`result.CountBySeverity(RenderGraphValidationSeverity::Error) == 0u` (combined
with the compiler-level findings already stored on the render graph) to the
device exactly once. `VulkanDevice` stores the bit in
`m_LatestRecipeValidationClean` (`std::atomic<bool>`, initialized to `false`
and reset to `false` in `Initialize()`) and `BuildOperationalInputs()` reads
it as `inputs.BarrierValidationClean`. A failed recipe build or a failed
`RenderGraph::Compile()` publishes `false` so the gate cannot inherit a
stale-clean state. The remaining higher gate (`PublicServiceReconciled`)
stays `false` until its owning slice (`GRAPHICS-033F`) lands, so the evaluator
preserves the existing fail-closed contract.

Ordered gate checklist:

1. Build/run gate is reconciled: the Vulkan backend is compiled when requested,
   `RenderConfig::EnablePromotedVulkanDevice == true`, and runtime requested
   Vulkan instead of Null.
2. volk, `VkInstance`, platform surface, selected physical device, and recorded
   graphics/present/transfer queue-family indices are live.
3. Logical device, required Vulkan 1.2/1.3 feature chain, `VK_KHR_swapchain`, VMA
   allocator, and heap-budget diagnostics are live.
4. Swapchain create/acquire/present/resize/recreate paths satisfy the
   GRAPHICS-013CQ fullscreen-present contract, including format, color-space,
   present-mode, and device-lost policy.
5. Per-frame command pools, primary command buffers, fences, acquire/render
   semaphores, and the transfer timeline-semaphore path are live for the
   GRAPHICS-032 minimal recipe.
6. `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` recording bodies
   execute against the real command context and match the CPU/null recipe
   command contract.
7. Barrier/layout validation for `SceneColorHDR`, `SceneDepth`, imported
   backbuffer finalization, and transfer uploads reports no GRAPHICS-022 hard
   errors.
8. Public service fallback reconciliation is complete: bindless heap, transfer
   queue, pipeline manager, swapchain/backbuffer import, and command context all
   report one consistent operational answer.
9. Validation-layer policy has run for the gate check; validation errors during
   the gate force fail-closed status.

Status is append-only and reasoned rather than bool-only:
`VulkanOperationalStatusCode { NotCompiled, NotRequested,
RequestedButUnsupported, RequestedButFailedInit, RequestedButValidationFailed,
RequestedButIncompleteGate, Operational }`. `VulkanOperationalReason` records the
first failing gate (for example `MissingInstance`, `MissingSurface`,
`NoSuitablePhysicalDevice`, `MissingRequiredExtension`, `MissingRequiredFeature`,
`LogicalDeviceFailed`, `AllocatorFailed`, `SwapchainFailed`,
`CommandSyncFailed`, `MinimalRecipeRecordingMissing`, `BarrierValidationFailed`,
`PublicServiceReconciliationFailed`, `ValidationLayerError`, `DeviceLost`, or
`SurfaceLost`). Future gates append reasons without rewriting existing values.

Runtime reconciliation is fixed by this truth table:

| CompiledIn | Requested | HostSupports | InitSucceeded | GateStatus | Effective device | Counter increment | Warn breadcrumb | Runtime result |
|---|---|---|---|---|---|---|---|---|
| false | false | n/a | n/a | `NotRequested` | Null | none | no | continue |
| false | true | n/a | n/a | `NotCompiled` | Null | `VulkanFallbackToNullCount` | `VulkanRequestedButNotOperational` once | continue |
| true | false | n/a | n/a | `NotRequested` | Null | none | no | continue |
| true | true | false | false | `RequestedButUnsupported` | Null | `VulkanFallbackToNullCount` | `VulkanRequestedButNotOperational` once | continue |
| true | true | true | false | `RequestedButFailedInit` | Null | `VulkanFallbackToNullCount`, `VulkanInitFailureCount` | `VulkanRequestedButNotOperational` once | continue |
| true | true | true | true | `RequestedButValidationFailed` | Null | `VulkanFallbackToNullCount`, `VulkanValidationErrorCount` | `VulkanRequestedButNotOperational` once | continue |
| true | true | true | true | `RequestedButIncompleteGate` | Null | `VulkanFallbackToNullCount`, `VulkanOperationalGateFailureCount` | `VulkanRequestedButNotOperational` once | continue |
| true | true | true | true | `Operational` | Vulkan | none | no | continue |

Runtime never aborts solely because requested Vulkan falls back to Null. The
diagnostic surface is a Vulkan-owned `VulkanOperationalDiagnosticsSnapshot` with
process-monotonic counters (`VulkanFallbackToNullCount`,
`VulkanInitFailureCount`, `VulkanValidationErrorCount`,
`VulkanOperationalGateFailureCount`, `VulkanDeviceLostOperationalDropCount`) and
a fixed-size reason histogram (`ReasonHistogram`, one
`std::uint32_t` slot per `VulkanOperationalReason`). The snapshot lives in
`Backends.Vulkan.cppm` next to `FallbackDiagnosticsSnapshot` and is queried
through `GetVulkanOperationalDiagnosticsSnapshot()`. Counters are bumped by
`RecordVulkanOperationalFallback(status)` from the runtime startup
breadcrumb call site in `Runtime::Engine::Initialize()` (one call per
non-operational truth-table row per startup), and
`NoteVulkanOperationalDeviceLostDrop()` is invoked from
`VulkanDevice::NoteDeviceLostIfNeeded()` on the operational→non-operational
device-lost transition. Counters are process-monotonic across
`Initialize`/`Shutdown` cycles. Renderer code still gates only on
`IDevice::IsOperational()` and backend-neutral render-graph stats.

## Module surface

| Module | Exported API |
|---|---|
| `Extrinsic.Backends.Vulkan` | `CreateVulkanDevice()`, `GetVulkanBootstrapDiagnosticsSnapshot()`, `VulkanBootstrapStatus`, `VulkanBootstrapDiagnosticsSnapshot`, `GetVulkanFrameLifecycleDiagnosticsSnapshot()`, `VulkanFrameBeginStatus`, `VulkanFrameEndStatus`, `VulkanFramePresentStatus`, `VulkanFrameResizeStatus`, `VulkanFrameLifecycleDiagnosticsSnapshot`, `GetVulkanServiceDiagnosticsSnapshot()`, `VulkanServiceBootstrapStatus`, `VulkanServiceDiagnosticsSnapshot`, `GetVulkanPipelineDiagnosticsSnapshot()`, `VulkanPipelineCreationStatus`, `VulkanPipelineDiagnosticsSnapshot`, `GetFallbackBindlessAllocationAttemptCount()`, `GetFallbackTransferUploadAttemptCount()`, `GetFallbackPipelineCreationAttemptCount()`, `GetFallbackBeginFrameAttemptCount()`, `GetFallbackEndFrameAttemptCount()`, `GetFallbackPresentAttemptCount()`, `GetFallbackResizeAttemptCount()`, `GetFallbackCommandRecordingAttemptCount()`, `GetLastFallbackPipelineReason()`, `FallbackPipelineReason`, `GetFallbackDiagnosticsSnapshot()`, `FallbackDiagnosticsSnapshot`, `EvaluateVulkanOperationalStatus()`, `VulkanOperationalInputs`, `VulkanOperationalStatus`, `VulkanOperationalStatusCode`, `VulkanOperationalReason`, `ToString(VulkanOperationalStatusCode)`, `ToString(VulkanOperationalReason)`, `kVulkanOperationalReasonCount`, `VulkanOperationalDiagnosticsSnapshot`, `GetVulkanOperationalDiagnosticsSnapshot()`, `RecordVulkanOperationalFallback()`, `NoteVulkanOperationalDeviceLostDrop()`, `EvaluateVulkanDeviceOperationalStatus()` |
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
| `Backends.Vulkan.OperationalStatus.cpp` | §10 `EvaluateVulkanOperationalStatus()` — pure CPU evaluator of the 9-step operational gate consumed by `VulkanDevice::IsOperational()` (GRAPHICS-033A). |
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


# GRAPHICS-018Q — Vulkan integration clarification follow-ups

## Goal
Capture Vulkan renderer integration questions that should not be implemented inside unrelated `GRAPHICS-018` plumbing slices.

## Non-goals
- No C++ behavior changes.
- No Vulkan smoke-test implementation.
- No changes to renderer/RHI ownership policy beyond documenting decisions.

## Context
`GRAPHICS-018` is active for wiring concrete Vulkan execution behind promoted graphics interfaces. The current implementation can proceed with CPU/null contract coverage while several environment- and backend-specific details remain open. Entries are split by whether they block the next operational Vulkan bring-up slice or are nonblocking clarification/follow-up work.

## Required changes

### Blocks next operational Vulkan slice

- Reconcile fail-closed fallback bindless heap and transfer queue behavior with real Vulkan services before `VulkanDevice::IsOperational()` can become true; the renderer/runtime reset prerequisite is complete in `tasks/done/GRAPHICS-018R-operational-transition.md`.
- Define shader source packaging and pipeline asset path policy for real pass rendering in Vulkan smoke tests.
- Decide the promoted depth-prepass shader asset path/packaging policy and whether a dedicated depth-only shader should be introduced before enabling real Vulkan depth-prepass smoke rendering.
- Decide ownership of dynamic-rendering attachment scope and transient texture materialization for `DepthPrepass`, `SurfacePass`, and downstream real pass command bodies.
- Define operational resize, acquire, present, and device-loss diagnostic taxonomy, including which diagnostics belong in `GRAPHICS-018` versus rendergraph validation hardening in `GRAPHICS-022`. *Resolved for fail-closed resize/presentation only:* `VulkanDevice::Resize` now exposes `GetFallbackResizeAttemptCount()` and `FallbackDiagnosticsSnapshot::ResizeAttempts` while still recording the requested extent for CPU/runtime diagnostics; `VulkanDevice::Present` exposes `GetFallbackPresentAttemptCount()` and `FallbackDiagnosticsSnapshot::PresentAttempts` so CPU CI can observe presentation loops against a non-operational device/swapchain. Operational resize/acquire/present/device-loss taxonomy remains open.

### Nonblocking clarifications and follow-ups

- Decide the platform/window fixture policy for opt-in swapchain smoke tests beyond guarded bootstrap, including headless CI behavior and skip diagnostics when no surface-capable device is available. *Resolved for guarded bootstrap:* `VulkanBootstrapSmoke.InitializeCreatesPerFrameResourcesOrFailsCleanly` is labeled `gpu;vulkan`, uses the GLFW platform only when it can initialize and create a native window, and skips rather than failing on window-unavailable hosts.
- Decide the exact opt-in smoke-test assertion boundary for the later operational swapchain/device bring-up path (`BeginFrame()` succeeds with a valid backbuffer handle and presentation/acquire diagnostics are meaningful). *Resolved for guarded bootstrap:* `VulkanDevice::Initialize()` may create an instance/surface, probe a surface-capable physical device, create a logical device, acquire graphics/present/transfer queues, create a VMA allocator, allocate per-frame command/sync resources, and create/register swapchain images/views/handles, but `IsOperational()` must remain false; the smoke assertion boundary is either `VulkanBootstrapStatus::RegisteredSwapchainImages` with logical-device/queue/allocator/per-frame/swapchain diagnostics set, a structured swapchain/logical-device/allocator/per-frame failure after a suitable physical device was selected, or a structured clean failure before physical-device selection.
- Decide whether concrete Vulkan pipeline creation should land before or after swapchain bring-up, including the minimal CPU-testable diagnostics for currently fail-closed `CreatePipeline` paths.
- Decide the texture upload policy beyond the current synchronous `WriteTexture()` path, including async transfer-queue use, per-subresource layout tracking, mip-chain/cubemap batching, and opt-in smoke-test assertions; batching implementation is tracked by `GRAPHICS-018T`.
- Decide sampler feature policy for opt-in Vulkan smoke coverage, including anisotropy feature negotiation and maximum supported anisotropy clamping. *Resolved (border color):* `GRAPHICS-018S` added backend-neutral `RHI::SamplerBorderColor` / `SamplerDesc::BorderColor`, preserved the opaque-black default, and maps it to Vulkan `VkBorderColor` in the backend without requiring GPU execution.
- Decide whether fail-closed fallback counters (`GetFallbackBindlessAllocationAttemptCount`, `GetFallbackTransferUploadAttemptCount`, `GetFallbackPipelineCreationAttemptCount`, `GetFallbackBeginFrameAttemptCount`, `GetFallbackEndFrameAttemptCount`, `GetFallbackPresentAttemptCount`, `GetFallbackResizeAttemptCount`) should also expose a structured "last reason" enum (extension/feature negotiation vs. device-loss vs. pre-bring-up vs. shader missing) or whether per-counter granularity plus log breadcrumbs is sufficient. *Pilot landed for `CreatePipeline` only:* `GetLastFallbackPipelineReason()` returns a `FallbackPipelineReason` (`None`, `PreBringUp`, `ShaderMissing`) because two distinct fail-closed reasons already exist there; bindless, transfer-queue, `BeginFrame`, `EndFrame`, `Present`, and `Resize` fallbacks deliberately stay counter-only until a second reason emerges or operational bring-up introduces extension/feature-negotiation and device-loss reasons. The remaining decision is whether to extend the enum (or introduce parallel ones) once those additional reasons appear.
- *Resolved (aggregation):* `GetFallbackDiagnosticsSnapshot()` returns a `FallbackDiagnosticsSnapshot` POD aggregating all seven counters plus `LastPipelineReason` so consumers can read all eight fields with one call. Individual getters are retained as the canonical primitives. The aggregate read is not tear-free across fields; each field is loaded with relaxed atomics in a fixed order (bindless, transfer, pipeline-count, last-pipeline-reason, begin-frame-count, end-frame-count, present-count, resize-count). Asserted by `VulkanFailClosedContract.FallbackDiagnosticsSnapshotMatchesIndividualGetters` and `VulkanFailClosedContract.FallbackDiagnosticsSnapshotIsProcessMonotonic` in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`.
- *Resolved (BeginFrame fail-closed counter):* `VulkanDevice::BeginFrame` on a non-operational device (or before swapchain bring-up) now increments a process-monotonic `GetFallbackBeginFrameAttemptCount()` counter and emits a structured log breadcrumb, mirroring the bindless/transfer/pipeline counter pattern. Also exposed as `FallbackDiagnosticsSnapshot::BeginFrameAttempts`. Locked in by `VulkanFailClosedContract.BeginFrameOnNonOperationalDeviceIncrementsAttemptCounter`, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. This lets CPU CI surface accidental runtime/renderer frame loops driving a fail-closed Vulkan device.
- *Resolved (EndFrame fail-closed counter and frame-loop pair):* `VulkanDevice::EndFrame` on a non-operational device now takes a fail-closed early-return path that increments a process-monotonic `GetFallbackEndFrameAttemptCount()` counter and emits a structured log breadcrumb, mirroring the `BeginFrame` counter. Also exposed as `FallbackDiagnosticsSnapshot::EndFrameAttempts`, appended after `BeginFrameAttempts` in the snapshot field order. Together with `GetFallbackBeginFrameAttemptCount()` this completes the frame-loop fail-closed pair. Locked in by `VulkanFailClosedContract.EndFrameOnNonOperationalDeviceIncrementsAttemptCounter` plus `VulkanFailClosedContract.BeginEndFrameCountersTrackPairwiseOnNonOperationalDevice`, which asserts that paired Begin/End calls advance both counters in lockstep, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. This catches an unbalanced renderer/runtime frame loop driving a fail-closed Vulkan device on CPU CI even when only one half of the pair is wired.
- *Resolved (Present fail-closed counter):* `VulkanDevice::Present` on a non-operational device or before swapchain bring-up now takes a fail-closed early-return path that increments a process-monotonic `GetFallbackPresentAttemptCount()` counter and emits a structured log breadcrumb. Also exposed as `FallbackDiagnosticsSnapshot::PresentAttempts`, appended after `EndFrameAttempts` in the snapshot field order. Locked in by `VulkanFailClosedContract.PresentOnNonOperationalDeviceIncrementsAttemptCounter` plus `VulkanFailClosedContract.BeginEndPresentCountersTrackLifecycleOnNonOperationalDevice`, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. This catches runtime presentation loops driving a fail-closed Vulkan device even when Begin/End behavior is separately covered.
- *Resolved (Resize fail-closed counter):* `VulkanDevice::Resize` on a non-operational device or before swapchain bring-up now increments a process-monotonic `GetFallbackResizeAttemptCount()` counter, emits a structured log breadcrumb, and still records the requested extent for CPU/runtime diagnostics. Also exposed as `FallbackDiagnosticsSnapshot::ResizeAttempts`, appended after `PresentAttempts` in the snapshot field order. Locked in by `VulkanFailClosedContract.ResizeOnNonOperationalDeviceIncrementsAttemptCounterAndRecordsExtent`, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. Operational swapchain recreation remains separate from this fail-closed breadcrumb.
- Decide whether per-call breadcrumb logging on fallback bindless/transfer/pipeline paths should remain (currently one warn line per call) or be rate-limited / once-only when callsites are expected to fire many times per frame before bring-up.
- Confirm that fail-closed fallback counters should remain process-monotonic across `Initialize`/`Shutdown` cycles (current behavior) rather than being reset per device instance; required for diagnostics that span full-engine restarts of the Vulkan backend. *Resolved (current behavior locked in):* process-monotonic accumulation across `Initialize`/`Shutdown` cycles and across `VulkanDevice` instance destruction/re-creation, plus persistence of `GetLastFallbackPipelineReason()` across `Shutdown`, are now asserted by `VulkanFailClosedContract.FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles` in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`, so a future refactor cannot silently switch any counter or the last-reason value to instance-scoped state.

## Tests
- Documentation/link checks only when this clarification task is edited.

## Docs
- Update `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, or GPU verification docs with any finalized policy.

## Acceptance criteria
- Each question above is either resolved in docs or split into a concrete implementation task.
- Clarifications do not require Vulkan in the default CPU correctness gate.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not mix this docs-only clarification task with C++ behavior work.
- Do not make Vulkan/GPU tests mandatory in the default CPU gate.


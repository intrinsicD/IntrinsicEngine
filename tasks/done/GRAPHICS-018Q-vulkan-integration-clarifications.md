# GRAPHICS-018Q — Vulkan integration clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-017Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-07.
- Branch: `claude/agentic-workflow-session-qg1Wv`.
- Promotion commit: `e38257c` (move file from `tasks/backlog/rendering/` to `tasks/active/`).
- Implementation commit: `2ca4e15` (record the four remaining clarification decisions on the active task file and sync `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, `src/graphics/renderer/README.md`, and `src/graphics/vulkan/README.md`; refresh the rendering backlog README description).
- Task-state commit: this retirement commit (moves the file from `tasks/active/` to `tasks/done/` and redirects the rendering backlog README link).
- Resolution: decisions recorded below and consequential notes synced into `docs/architecture/graphics.md` (extended Vulkan-backend bullet with a new "Per `GRAPHICS-018Q`" paragraph capturing the four decisions next to the existing Vulkan operational summary), `docs/architecture/rendering-three-pass.md` (new "Vulkan integration follow-up contract" subsection alongside the existing `GRAPHICS-013AQ`/`013BQ`/`013CQ`/`014Q`/`015Q`/`017Q` clarification paragraphs), `src/graphics/renderer/README.md` (new ownership-contract bullet next to the existing `GpuAssetCache`/`MaterialSystem` description), and `src/graphics/vulkan/README.md` (extended texture-upload, sampler, fallback-reason, and breadcrumb paragraphs with cross-links to `GRAPHICS-018Q` and `GRAPHICS-018T`). The rendering backlog README entry for `GRAPHICS-018Q` is redirected from `tasks/backlog/rendering/` to `tasks/active/` by the promotion commit and from `tasks/active/` to `tasks/done/` by this retirement commit. No `docs/migration/nonlegacy-parity-matrix.md` rows change because the four decisions are backend-internal Vulkan policy rather than legacy-vs-promoted parity entries. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .`.

## Decisions

- **Texture upload policy beyond synchronous `WriteTexture()`.** The
  promoted Vulkan backend keeps the current guarded synchronous
  staging-buffer one-subresource `WriteTexture()` path as the
  fail-closed correctness baseline; runtime/streaming uploads must use
  `RHI::ITransferQueue` (the canonical seam declared by `GRAPHICS-026`)
  rather than the blocking graphics-queue helper. Per-subresource
  layout tracking stays whole-image until multi-subresource batching
  lands: backends record one tracked `VulkanImage::CurrentLayout` per
  texture and transition the whole image around uploads, so callers do
  not need to reason about per-mip / per-layer state. Multi-mip,
  multi-layer, and cubemap batching (`VkBufferImageCopy` region
  coalescing, async transfer-queue submission, optional per-subresource
  layout tracking when needed) is owned by
  [`GRAPHICS-018T`](GRAPHICS-018T-texture-upload-batching.md);
  this clarification freezes the policy seam but does not implement
  batching. Opt-in `gpu;vulkan` smoke for multi-mip / multi-layer /
  cubemap uploads landed with `GRAPHICS-018T`; the existing
  single-subresource opt-in smoke remains in place for the simple path.
  No new RHI surface is added by this clarification; `WriteTexture()`,
  `ITransferQueue`, and `TextureManager::Reupload()` already provide
  the seams `GRAPHICS-018T` extended.

- **Sampler anisotropy feature negotiation and clamping.**
  Anisotropy stays expressed through the existing backend-neutral
  `RHI::SamplerDesc::MaxAnisotropy` float; no new RHI-visible enum,
  cap query, or backend-feature handle is introduced. The Vulkan
  backend probes `VkPhysicalDeviceFeatures::samplerAnisotropy` during
  physical-device selection alongside the existing required Vulkan
  1.2 / 1.3 feature negotiation (descriptor indexing, timeline
  semaphores, buffer device addresses, dynamic rendering) and enables
  `samplerAnisotropy` on the logical device when supported.
  `GetVulkanBootstrapDiagnosticsSnapshot()` records anisotropy
  support / enablement next to the other required features so CPU
  contract coverage can observe the negotiated state without exposing
  Vulkan-native types. At sampler-creation time the backend consumes
  `SamplerDesc::MaxAnisotropy` as follows: when the device feature is
  unsupported or `MaxAnisotropy <= 1.0`, anisotropy is silently
  disabled (`anisotropyEnable = VK_FALSE`); otherwise the backend
  clamps the requested value to
  `min(MaxAnisotropy, VkPhysicalDeviceLimits::maxSamplerAnisotropy)`
  and emits a single warn breadcrumb when clamping reduces the value,
  preserving deterministic behavior on hardware whose anisotropy cap
  is below the requested level. Anisotropy is treated as a quality
  hint rather than a correctness requirement, so missing support
  never fails sampler creation; this matches the established pattern
  for other optional sampler state and keeps the renderer / RHI
  callers identical across hosts. `GRAPHICS-018S` already locked in
  the matching `SamplerBorderColor` pattern (RHI-visible enum,
  backend-local `VkBorderColor` mapping, no GPU execution required),
  so anisotropy follows the same fail-soft / diagnostics-only
  convention.

- **`FallbackPipelineReason` enum extension policy.** Each fail-closed
  fallback counter on `VulkanDevice` continues to expose its own
  scoped reason enum 1:1, rather than promoting a single mega-enum to
  cover every counter. The pilot pattern set by
  `GRAPHICS-018`/`GetLastFallbackPipelineReason()` is canonical:
  introduce a path-local `FallbackXxxReason` enum only when a *second*
  distinct fail-closed reason actually emerges in that path. Today
  bindless, transfer-queue, `BeginFrame`, `EndFrame`, `Present`, and
  `Resize` fallbacks all have a single fail-closed reason
  (pre-bring-up / non-operational), so they remain counter-only and
  no enum is added. When operational bring-up adds a second reason in
  the pipeline path (extension / feature-negotiation failure or
  device-loss after pipeline creation has succeeded once), the new
  variant is appended to the existing `FallbackPipelineReason` enum
  rather than introduced as a parallel enum, because the reason is
  scoped to that path. When a second reason emerges in any other
  counter, a new path-local enum named after that counter
  (e.g. `FallbackBindlessReason`, `FallbackTransferUploadReason`,
  `FallbackBeginFrameReason`) is introduced and a matching
  `LastBindlessReason` / `LastTransferUploadReason` /
  `LastBeginFrameReason` field is appended to
  `FallbackDiagnosticsSnapshot` after the existing eight fields,
  preserving the established field-order extension convention from
  the prior 018Q resolutions. This keeps each counter and its reason
  enum 1:1 to its path so consumers do not have to demultiplex a
  combined enum. Counters themselves remain process-monotonic across
  `Initialize`/`Shutdown` cycles independent of any reason field, as
  already locked in by `VulkanFailClosedContract.FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles`.

- **Per-call breadcrumb logging rate-limiting.** Per-call warn
  breadcrumbs on the bindless, transfer-queue, and pipeline-creation
  fallback paths remain canonical for now: those callsites fire
  infrequently while non-operational and the visibility helps catch
  accidental loops or missing operational gating before bring-up.
  Frame-loop counters (`BeginFrame`, `EndFrame`, `Present`) keep the
  existing once-per-fail-closed-cycle rate-limited breadcrumb policy
  already locked in by the 018 frame-lifecycle work (one warn line on
  the first fire of each cycle to avoid 60 Hz log spam, with the
  process-monotonic counter on `FallbackDiagnosticsSnapshot` carrying
  the precise diagnostic regardless of breadcrumb suppression);
  `Resize` likewise stays unrate-limited because window-resize events
  are inherently low-frequency. When operational bring-up demonstrates
  many-per-frame fallback firing in a counter that currently uses
  per-call breadcrumbs, migration to once-per-frame rate-limited
  breadcrumbs (with a cumulative-skipped count appended to the next
  emitted breadcrumb) is a separate semantic task scoped to that
  counter, not a sweeping retrofit, and must keep the underlying
  process-monotonic counter exact. External diagnostics consumers
  always read counts from `FallbackDiagnosticsSnapshot`, never from
  log breadcrumb cardinality, so any future rate-limiting change is
  invisible to CPU contract coverage.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/graphics.md` (extended Vulkan-backend bullet
  with a new "Per `GRAPHICS-018Q`" paragraph capturing the four
  decisions next to the existing Vulkan operational summary),
  `docs/architecture/rendering-three-pass.md` (new
  "### Vulkan integration follow-up contract" subsection alongside
  the existing `GRAPHICS-013AQ`/`013BQ`/`013CQ`/`014Q`/`015Q`/`017Q`
  clarification paragraphs), `src/graphics/renderer/README.md`
  (matching ownership-contract bullet near the existing
  operational-transition paragraph), and
  `src/graphics/vulkan/README.md` (extended texture-upload, sampler,
  fallback-reason, and breadcrumb paragraphs with cross-links to
  `GRAPHICS-018Q` and `GRAPHICS-018T`). The rendering backlog README
  entry for `GRAPHICS-018Q` is redirected from the
  `tasks/backlog/rendering/` path to the `tasks/active/` path by the
  promotion commit and will be redirected to `tasks/done/` by the
  retirement commit; the entry's bullet description is also expanded
  by this commit to summarize the four resolved decisions.
- No `docs/migration/nonlegacy-parity-matrix.md` rows change because
  texture upload, sampler anisotropy, fallback reason taxonomy, and
  breadcrumb policy are all backend-internal Vulkan policy rather
  than legacy-vs-promoted parity entries.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

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

- [x] *Resolved (predicate-gated public service handoff):* `VulkanDevice::IsOperational()` is now backed by a conservative Vulkan-owned predicate. The live-prerequisite half requires logical device, surface/swapchain/images/handles, per-frame command/sync resources, queue handles, global pipeline layout, live bindless heap, live transfer queue, command-context rebinding prerequisites, and non-device-lost state; the safety-prerequisite half remains false until canonical renderer resource/descriptor/pass execution lands. Public `GetBindlessHeap()`/`GetTransferQueue()` return live services only when the full predicate is true and otherwise remain fail-closed fallbacks. The renderer/runtime reset prerequisite is complete in `tasks/done/GRAPHICS-018R-operational-transition.md`.
- [x] Define live bindless heap / transfer queue handoff semantics once the promoted Vulkan device becomes operational. *Resolved for guarded bootstrap prerequisites and public accessor policy:* physical-device selection now requires sampled-image descriptor indexing update-after-bind/partially-bound, timeline semaphores, buffer device addresses, and dynamic rendering, the logical device enables those features before swapchain registration, successful guarded bootstrap now creates internal live bindless/global-layout/transfer service state reported by `GetVulkanServiceDiagnosticsSnapshot()`, and public service access is predicate-gated. Exposing the live services publicly remains blocked only because canonical frame resources, descriptors, and pass command bodies are not yet operational.
- [x] *Resolved (shader packaging for guarded resource readiness):* shader sources for promoted Vulkan smoke remain under `assets/shaders/` and opt-in smoke compiles required SPIR-V with `glslc --target-env=vulkan1.3` when the compiler is available; missing `glslc` skips only the shader/pipeline sub-check and does not affect the default CPU gate.
- [x] *Resolved (depth-prepass shader source):* `assets/shaders/depth_prepass.vert` is the dedicated BDA scene-table depth-prepass vertex shader source for the promoted path. The guarded smoke test compiles it to temporary SPIR-V and verifies depth-only dynamic-rendering graphics pipeline creation while `IsOperational()` remains false.
- [x] *Resolved (guarded attachment materialization boundary):* section 3 owns direct backend materialization of canonical depth/color attachment textures and depth-only graphics pipeline state after service-ready bootstrap. Full render-graph attachment lifetime integration, barriers, layout transitions, and pass command bodies remain owned by `GRAPHICS-018` section 4 rather than this clarification task.
- [x] *Resolved (guarded lifecycle, resize, and device-loss taxonomy):* `VulkanDevice::BeginFrame`, `EndFrame`, `Present`, and `Resize` still expose process-monotonic fallback counters through `FallbackDiagnosticsSnapshot`, and `GetVulkanFrameLifecycleDiagnosticsSnapshot()` reports structured backend-local fail-closed statuses (`SkippedNotOperational`, `SkippedNoSwapchain`, `SkippedNoSwapchainImages`, pending resize states), guarded direct acquire/submit/present statuses (`Acquired`, `Submitted`, `Presented`, `Suboptimal`, `OutOfDate`, and failure variants), guarded swapchain recreation statuses (`RecordedPendingRecreate`, `Recreated`, `FailedRecreate`), and pending-resize/device-lost booleans without exposing `Vk*` types. Direct service-ready bootstrap can now exercise empty acquire -> command record -> submit -> present, zero-extent resize deferral, and nonzero swapchain recreation while `IsOperational()` remains false. Operational promotion for canonical renderer resource/descriptor/pass execution still needs to land before `GRAPHICS-018` can finish.

### Nonblocking clarifications and follow-ups

- [x] Decide the platform/window fixture policy for opt-in swapchain smoke tests beyond guarded bootstrap, including headless CI behavior and skip diagnostics when no surface-capable device is available. *Resolved for guarded bootstrap:* `VulkanBootstrapSmoke.InitializeCreatesPerFrameResourcesOrFailsCleanly` is labeled `gpu;vulkan`, uses the GLFW platform only when it can initialize and create a native window, and skips rather than failing on window-unavailable hosts.
- [x] Decide the exact opt-in smoke-test assertion boundary for the later operational swapchain/device bring-up path (`BeginFrame()` succeeds with a valid backbuffer handle and presentation/acquire diagnostics are meaningful). *Resolved for guarded bootstrap, direct empty-frame smoke, and guarded resize smoke:* `VulkanDevice::Initialize()` may create an instance/surface, probe a surface-capable physical device with required Vulkan 1.2/1.3 features, create a logical device with descriptor indexing/timeline semaphore/buffer-device-address/dynamic-rendering enabled, acquire graphics/present/transfer queues, create a VMA allocator, allocate per-frame command/sync resources, and create/register swapchain images/views/handles, but `IsOperational()` must remain false. When service diagnostics report `Ready`, opt-in smoke coverage may directly acquire a swapchain image, record an empty command buffer that transitions the acquired backbuffer to present layout, submit it, present it, defer zero-extent resize, and recreate the swapchain for a nonzero extent. The assertion boundary remains either successful guarded bootstrap plus direct lifecycle success/clean acquire or recreate skip/failure with structured diagnostics, a structured swapchain/logical-device/allocator/per-frame failure after a suitable physical device was selected, or a structured clean failure before physical-device selection.
- [x] *Resolved (guarded pipeline creation before operational frames):* concrete Vulkan pipeline creation now lands after guarded logical-device/global-layout service bootstrap but before `IsOperational()` becomes true. `VulkanDevice::CreatePipeline()` reads SPIR-V files from `RHI::PipelineDesc`, creates shader modules, and builds compute or dynamic-rendering graphics pipelines for direct backend smoke coverage; RHI managers and renderer execution remain gated on `IDevice::IsOperational()`. `GetVulkanPipelineDiagnosticsSnapshot()` provides CPU-visible pre-bring-up, invalid-description, shader-read/module, pipeline-failure, and success statuses. Real pass shader packaging for canonical renderer pipelines remains a separate blocker below.
- [x] *Resolved (policy seam frozen, batching deferred to `GRAPHICS-018T`):* the guarded synchronous staging-buffer one-subresource `WriteTexture()` path stays the fail-closed correctness baseline, runtime/streaming uploads must use `RHI::ITransferQueue` (the seam declared by `GRAPHICS-026`) rather than the blocking graphics-queue helper, per-subresource layout tracking stays whole-image for the batched path, and multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan` smoke landed with [`GRAPHICS-018T`](GRAPHICS-018T-texture-upload-batching.md). No new RHI surface is added by this clarification. See "Decisions" above.
- [x] *Resolved (anisotropy feature negotiation and clamping):* anisotropy stays expressed through the existing `RHI::SamplerDesc::MaxAnisotropy` float; the Vulkan backend probes `VkPhysicalDeviceFeatures::samplerAnisotropy` during physical-device selection alongside the existing required Vulkan 1.2 / 1.3 features, enables it on the logical device when supported, records support / enablement on `GetVulkanBootstrapDiagnosticsSnapshot()`, and at sampler-creation time silently disables anisotropy when the feature is unsupported or `MaxAnisotropy <= 1.0`, otherwise clamps to `min(MaxAnisotropy, VkPhysicalDeviceLimits::maxSamplerAnisotropy)` with one warn breadcrumb when clamping reduces the value. Anisotropy is a quality hint and missing support never fails sampler creation. See "Decisions" above. *Resolved (border color):* `GRAPHICS-018S` added backend-neutral `RHI::SamplerBorderColor` / `SamplerDesc::BorderColor`, preserved the opaque-black default, and maps it to Vulkan `VkBorderColor` in the backend without requiring GPU execution.
- [x] Decide whether fail-closed fallback counters (`GetFallbackBindlessAllocationAttemptCount`, `GetFallbackTransferUploadAttemptCount`, `GetFallbackPipelineCreationAttemptCount`, `GetFallbackBeginFrameAttemptCount`, `GetFallbackEndFrameAttemptCount`, `GetFallbackPresentAttemptCount`, `GetFallbackResizeAttemptCount`) should also expose a structured "last reason" enum (extension/feature negotiation vs. device-loss vs. pre-bring-up vs. shader missing) or whether per-counter granularity plus log breadcrumbs is sufficient. *Pilot landed for `CreatePipeline` only:* `GetLastFallbackPipelineReason()` returns a `FallbackPipelineReason` (`None`, `PreBringUp`, `ShaderMissing`) because two distinct fail-closed reasons already exist there; bindless, transfer-queue, `BeginFrame`, `EndFrame`, `Present`, and `Resize` fallbacks deliberately stay counter-only until a second reason emerges or operational bring-up introduces extension/feature-negotiation and device-loss reasons. *Resolved (per-path scoped enums):* future device-loss / extension / feature-negotiation reasons in the pipeline-creation path are appended to the existing `FallbackPipelineReason` enum because they are scoped to that path; future second reasons in any other counter introduce a *new* path-local `FallbackXxxReason` enum named after that counter, with a matching `LastXxxReason` field appended to `FallbackDiagnosticsSnapshot` after the existing eight fields. Each counter and its reason enum stay 1:1 to its path so consumers do not have to demultiplex a combined enum, and counter process-monotonicity across `Initialize`/`Shutdown` cycles remains independent of any reason field. See "Decisions" above.
- [x] *Resolved (aggregation):* `GetFallbackDiagnosticsSnapshot()` returns a `FallbackDiagnosticsSnapshot` POD aggregating all seven counters plus `LastPipelineReason` so consumers can read all eight fields with one call. Individual getters are retained as the canonical primitives. The aggregate read is not tear-free across fields; each field is loaded with relaxed atomics in a fixed order (bindless, transfer, pipeline-count, last-pipeline-reason, begin-frame-count, end-frame-count, present-count, resize-count). Asserted by `VulkanFailClosedContract.FallbackDiagnosticsSnapshotMatchesIndividualGetters` and `VulkanFailClosedContract.FallbackDiagnosticsSnapshotIsProcessMonotonic` in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`.
- [x] *Resolved (BeginFrame fail-closed counter):* `VulkanDevice::BeginFrame` on a non-operational device (or before swapchain bring-up) now increments a process-monotonic `GetFallbackBeginFrameAttemptCount()` counter and emits a structured log breadcrumb, mirroring the bindless/transfer/pipeline counter pattern. Also exposed as `FallbackDiagnosticsSnapshot::BeginFrameAttempts`. Locked in by `VulkanFailClosedContract.BeginFrameOnNonOperationalDeviceIncrementsAttemptCounter`, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. This lets CPU CI surface accidental runtime/renderer frame loops driving a fail-closed Vulkan device.
- [x] *Resolved (EndFrame fail-closed counter and frame-loop pair):* `VulkanDevice::EndFrame` on a non-operational device now takes a fail-closed early-return path that increments a process-monotonic `GetFallbackEndFrameAttemptCount()` counter and emits a structured log breadcrumb, mirroring the `BeginFrame` counter. Also exposed as `FallbackDiagnosticsSnapshot::EndFrameAttempts`, appended after `BeginFrameAttempts` in the snapshot field order. Together with `GetFallbackBeginFrameAttemptCount()` this completes the frame-loop fail-closed pair. Locked in by `VulkanFailClosedContract.EndFrameOnNonOperationalDeviceIncrementsAttemptCounter` plus `VulkanFailClosedContract.BeginEndFrameCountersTrackPairwiseOnNonOperationalDevice`, which asserts that paired Begin/End calls advance both counters in lockstep, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. This catches an unbalanced renderer/runtime frame loop driving a fail-closed Vulkan device on CPU CI even when only one half of the pair is wired.
- [x] *Resolved (Present fail-closed counter):* `VulkanDevice::Present` on a non-operational device or before swapchain bring-up now takes a fail-closed early-return path that increments a process-monotonic `GetFallbackPresentAttemptCount()` counter and emits a structured log breadcrumb. Also exposed as `FallbackDiagnosticsSnapshot::PresentAttempts`, appended after `EndFrameAttempts` in the snapshot field order. Locked in by `VulkanFailClosedContract.PresentOnNonOperationalDeviceIncrementsAttemptCounter` plus `VulkanFailClosedContract.BeginEndPresentCountersTrackLifecycleOnNonOperationalDevice`, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. This catches runtime presentation loops driving a fail-closed Vulkan device even when Begin/End behavior is separately covered.
- [x] *Resolved (Resize fail-closed counter):* `VulkanDevice::Resize` on a non-operational device or before swapchain bring-up now increments a process-monotonic `GetFallbackResizeAttemptCount()` counter, emits a structured log breadcrumb, and still records the requested extent for CPU/runtime diagnostics. Also exposed as `FallbackDiagnosticsSnapshot::ResizeAttempts`, appended after `PresentAttempts` in the snapshot field order. Locked in by `VulkanFailClosedContract.ResizeOnNonOperationalDeviceIncrementsAttemptCounterAndRecordsExtent`, with cross-cycle persistence and snapshot agreement covered by the existing process-monotonic / snapshot tests in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`. Operational swapchain recreation remains separate from this fail-closed breadcrumb.
- [x] *Resolved (frame lifecycle diagnostics snapshot):* `GetVulkanFrameLifecycleDiagnosticsSnapshot()` provides a backend-local aggregate for the most recent promoted Vulkan `BeginFrame`/`EndFrame`/`Present`/`Resize` attempt: structured status enums, last frame/image indices, requested resize extent, last Vulkan result code, availability booleans, pending-resize/device-lost flags, and lifecycle counter values. CPU coverage in `VulkanFailClosedContract.FrameLifecycleDiagnosticsSnapshotReportsFailClosedStatuses` asserts fail-closed status/counter agreement; opt-in `VulkanBootstrapSmoke.InitializeCreatesPerFrameResourcesOrFailsCleanly` asserts guarded bootstrap can drive direct acquire/submit/present plus resize deferral/recreation with structured diagnostics while `IsOperational()` remains false. This resolves the lifecycle taxonomy shape for fail-closed and guarded direct paths; canonical renderer pass execution still owns the final operational promotion.
- [x] *Resolved (guarded internal service handoff):* after successful guarded swapchain image registration, `VulkanDevice::Initialize()` now creates internal live `VulkanBindlessHeap`, global pipeline-layout, `VulkanTransferQueue`/staging state, and rebinds command contexts to the live bindless descriptor set/layout. `GetVulkanServiceDiagnosticsSnapshot()` reports ready/failure/skipped states plus whether live operational prerequisites and safety prerequisites are satisfied. These live services are prerequisites only: public `GetBindlessHeap()` and `GetTransferQueue()` are routed through the same predicate as `IsOperational()`, so they still return fail-closed fallback services while the safety prerequisites remain false, and opt-in smoke coverage asserts that behavior.
- [x] *Resolved (guarded resource/descriptor readiness):* direct service-ready Vulkan smoke now creates canonical scene-table/storage/BDA buffers, indirect culling draw-bucket buffers, sampled/depth/color attachment textures, samplers, a sampled texture upload, a compute pipeline, and a depth-only graphics pipeline while `IsOperational()` remains false. `VulkanBindlessHeap` has a Vulkan-local texture/sampler resolver for queued descriptor writes, so future public bindless handoff can use RHI handles without adding Vulkan-native handles or live ECS knowledge to renderer/RHI APIs.
- [x] *Resolved (internal transfer hardening):* internal `VulkanTransferQueue` command-buffer allocation/begin/end/submit and timeline semaphore query failures now return invalid `RHI::TransferToken` values with logger diagnostics instead of aborting through `VK_CHECK_FATAL`. Uploads also preflight invalid service state, missing pools, source pointers/sizes, buffer ranges, texture mip/layer bounds, transfer-dst usage, and staging allocation before recording commands. This prepares future public transfer handoff; public `GetTransferQueue()` still returns the fail-closed fallback while non-operational.
- [x] *Resolved (command-context hardening):* `VulkanCommandContext` now tracks whether it has a live command buffer and active recording scope. Unbound or not-begun command recording calls skip with logger diagnostics and increment `GetFallbackCommandRecordingAttemptCount()` instead of issuing Vulkan commands against null/non-recording state. CPU coverage locks in that the graphics context returned through `IDevice::GetGraphicsContext()` stays fail-closed before operational bring-up.
- [x] *Resolved (per-call canonical for now, frame-loop already rate-limited):* per-call warn breadcrumbs on the bindless, transfer-queue, and pipeline-creation fallback paths remain canonical because those callsites fire infrequently while non-operational and the visibility helps catch accidental loops or missing operational gating before bring-up. Frame-loop counters (`BeginFrame`, `EndFrame`, `Present`) keep the existing once-per-fail-closed-cycle rate-limited breadcrumb policy already locked in by 018, with the process-monotonic counter on `FallbackDiagnosticsSnapshot` carrying the precise diagnostic regardless of breadcrumb suppression; `Resize` stays unrate-limited because window-resize events are inherently low-frequency. Migration of any per-call counter to once-per-frame rate-limited breadcrumbs (with a cumulative-skipped count appended to the next emission) is a separate semantic task scoped to that counter only when operational bring-up demonstrates many-per-frame fallback firing, never a sweeping retrofit, and must keep the underlying counter exact. See "Decisions" above.
- [x] Confirm that fail-closed fallback counters should remain process-monotonic across `Initialize`/`Shutdown` cycles (current behavior) rather than being reset per device instance; required for diagnostics that span full-engine restarts of the Vulkan backend. *Resolved (current behavior locked in):* process-monotonic accumulation across `Initialize`/`Shutdown` cycles and across `VulkanDevice` instance destruction/re-creation, plus persistence of `GetLastFallbackPipelineReason()` across `Shutdown`, are now asserted by `VulkanFailClosedContract.FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles` in `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`, so a future refactor cannot silently switch any counter or the last-reason value to instance-scoped state.

## Tests
- [x] Documentation/link checks only when this clarification task is edited.

## Docs
- [x] Update `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, or GPU verification docs with any finalized policy.

## Acceptance criteria
- [x] Each question above is either resolved in docs or split into a concrete implementation task.
- [x] Clarifications do not require Vulkan in the default CPU correctness gate.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not mix this docs-only clarification task with C++ behavior work.
- Do not make Vulkan/GPU tests mandatory in the default CPU gate.


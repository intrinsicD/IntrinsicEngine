# ADR 0004 — Vulkan Backend Bring-up and Fail-Closed Fallback

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics / Vulkan backend
- **Related tasks:** [`tasks/done/GRAPHICS-018`](../../tasks/done/GRAPHICS-018-vulkan-renderer-integration.md), [`GRAPHICS-018Q`](../../tasks/done/GRAPHICS-018Q-vulkan-integration-clarifications.md), [`GRAPHICS-018R`](../../tasks/done/GRAPHICS-018R-operational-transition.md), [`GRAPHICS-018T`](../../tasks/done/GRAPHICS-018T-texture-upload-batching.md), [`GRAPHICS-026`](../../tasks/done/GRAPHICS-026-vulkan-renderer-plumbing-followups.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/graphics/vulkan/README.md`](../../src/graphics/vulkan/README.md)
- **Supersedes:** none. Extracted from the `## Renderer/RHI frame lifecycle` mega-paragraph in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/backlog/architecture/DOCS-001-reduce-graphics-architecture-prose.md).

## Context

The promoted Vulkan backend lives behind the RHI `IDevice` seam alongside the Null device. Runtime composition must be able to request the promoted Vulkan device, observe a deterministic operational status, and recover by falling back to Null when the operational gate fails — without aborting startup, without leaking `Vk*` types through RHI or renderer code, and without coupling renderer logic to Vulkan-specific diagnostics.

This decision captures the shape of Vulkan backend bring-up, the fail-closed contract that guards `IDevice` callbacks before the operational gate is satisfied, the diagnostics surface that lets runtime/test code observe what happened, and the four `GRAPHICS-018Q` follow-up resolutions that finished the contract:

1. Texture upload policy under both blocking (`IDevice::WriteTexture`) and async (`RHI::ITransferQueue`) paths.
2. Sampler anisotropy probing/enable/clamp policy.
3. Fail-closed counter / reason-enum taxonomy.
4. Per-call vs frame-loop fail-closed log breadcrumb policy.

It is the canonical durable home for the Vulkan bring-up + fallback decision. `docs/architecture/graphics.md` retains a single pointer line to this ADR.

## Decision

### 1. Build + runtime selection gates

The promoted Vulkan device is selected only when **both** gates are enabled:

- Build option `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` (produces the `ExtrinsicBackendsVulkan` target).
- Runtime config `RenderConfig::EnablePromotedVulkanDevice == true`.

Otherwise `GraphicsBackend::Vulkan` requests route to the Null device, preserving the default CPU/null correctness path. The reconciliation truth table that drives the runtime breadcrumb lives in ADR-0005 (Vulkan operational readiness gate); this ADR owns only the bring-up + fail-closed contract that supports those gates.

### 2. Guarded `VulkanDevice::Initialize()` bring-up

`VulkanDevice::Initialize()` performs guarded Vulkan bootstrap/probing when given a native GLFW window:

- volk initialization.
- Instance creation.
- Surface creation.
- Physical-device / queue-family / swapchain-support probing.
- Required Vulkan 1.2 / 1.3 feature support **and** enablement for descriptor indexing, timeline semaphores, buffer device addresses, and dynamic rendering.
- Logical-device creation; graphics / present / transfer queue acquisition.
- VMA allocator creation.
- Per-frame command / sync resource allocation.
- Backend-local swapchain image / image-view / `RHI::TextureHandle` registration.
- Internal `VulkanBindlessHeap` / global pipeline-layout / `VulkanTransferQueue` creation.
- Command-context rebinding.

This entire bring-up remains backend-local and **does not by itself make Vulkan operational**.

### 3. Conservative `IsOperational()` predicate

`VulkanDevice::IsOperational()` is backed by a conservative backend-owned predicate that requires:

- Live logical-device / swapchain / per-frame / service state.
- Safety prerequisites still cleared for canonical renderer pass execution, synchronization / barrier validation, queue-family ownership handling where needed, and public service exposure.

Renderer and runtime code **must** branch on `IDevice::IsOperational()`, not Vulkan-specific diagnostics. The operational-transition reset seam (`IRenderer::RebuildOperationalResources()`, [`GRAPHICS-018R`](../../tasks/done/GRAPHICS-018R-operational-transition.md)) is present and CPU-tested, so a false → true transition produces a single backend-neutral reset/rebind that the renderer can act on without naming Vulkan.

### 4. Diagnostics snapshots (no native types leak)

Four CPU-only diagnostics snapshots expose backend-specific bring-up state without leaking Vulkan-native types through RHI or renderer code:

- **`GetVulkanBootstrapDiagnosticsSnapshot()`** — required-feature support / enablement, swapchain creation / image / view / handle counts, extent.
- **`GetVulkanServiceDiagnosticsSnapshot()`** — guarded internal service handoff; clean service-failure / skipped states; whether live operational prerequisites are present, whether safety prerequisites are cleared, whether public services are exposed.
- **`GetVulkanFrameLifecycleDiagnosticsSnapshot()`** — structured lifecycle statuses populated from all real paths (see §5 below).
- **`GetVulkanPipelineDiagnosticsSnapshot()`** — pre-bring-up skips, invalid descriptions, shader-read / module failures, pipeline creation failures, successful direct backend pipeline creation.

### 5. Fail-closed frame-lifecycle status taxonomy

`GetVulkanFrameLifecycleDiagnosticsSnapshot()` records backend-local structured statuses for every real path:

- Fail-closed paths emit `SkippedNotOperational` / `SkippedNoSwapchain` / `SkippedNoSwapchainImages`.
- Guarded direct paths emit:
  - `BeginFrame`: `Acquired` / `Suboptimal` / `OutOfDate` / `FailedAcquire`.
  - `EndFrame`: `Submitted` / `FailedSubmit`.
  - `Present`: `Presented` / `Suboptimal` / `OutOfDate` / `FailedPresent`.
  - `Resize`: `Recreated` / `FailedRecreate` / `RecordedPending*`.
- **Device-loss** from any path sets `DeviceLost` and routes subsequent calls back through fail-closed skips.

The snapshot also carries frame / image indices, requested resize extent, availability booleans, pending-resize / device-lost flags, and Vulkan result codes.

### 6. Rate-limited frame-loop breadcrumbs

Frame-loop fail-closed log breadcrumbs (`BeginFrame`, `EndFrame`, `Present`) are emitted **only on the first fire** per fail-closed cycle to avoid log spam at 60 Hz. CPU diagnostics counters remain process-monotonic regardless of breadcrumb suppression so consumers see precise diagnostics even when logs are quiet. `Resize` stays unrate-limited because resize events are user-driven and not in the hot loop.

### 7. Opt-in pre-operational smoke coverage

After service-ready bootstrap, opt-in Vulkan smoke coverage can directly:

- Acquire a swapchain image, record an empty command buffer, submit, present.
- Defer zero-extent resize; recreate the swapchain for a non-zero extent.
- Create canonical scene-table / draw-bucket buffers with BDA.
- Create sampled / depth / color attachment textures and samplers; upload a sampled texture subresource.
- Submit 2D multi-mip, 2D-array, and six-face cubemap full-chain uploads through the live transfer queue.
- Create compute and depth-only dynamic-rendering graphics pipelines.

All of this is possible **while `IsOperational()` remains false**. Renderer / runtime code still must not branch on these capabilities; they exist for `gpu;vulkan` smoke fixtures only.

### 8. Backend service exposure under `IsOperational() == false`

The two backend services that have opt-in pre-operational consumers expose distinct policies:

- **`GetBindlessHeap()`** is routed through `IDevice::IsOperational()` and returns the fallback heap while non-operational.
- **`GetTransferQueue()`** returns the live `VulkanTransferQueue` once guarded live prerequisites are ready, even while `IsOperational()` remains false, so opt-in upload smoke and streaming seams can exercise async transfer without special-casing Vulkan. Pre-bootstrap / non-service-ready states still return the fail-closed fallback queue.

The live internal `VulkanBindlessHeap` resolves backend-owned RHI texture / sampler handles into descriptor writes through a Vulkan-local resolver, preserving texture / material descriptor readiness without exposing `Vk*` handles or live ECS knowledge outside `src/graphics/vulkan`.

### 9. Fail-closed return values (never abort)

The internal transfer queue returns invalid `RHI::TransferToken` values with logger diagnostics instead of aborting on command-buffer, submit, semaphore-query, range, or staging failures. Device-lost results from lifecycle, recreate, one-shot upload, and resource / pipeline creation paths return the backend to the fail-closed state. Unbound / not-begun Vulkan command-context recording calls skip with `GetFallbackCommandRecordingAttemptCount()` diagnostics instead of issuing commands against null state.

### 10. GRAPHICS-018Q follow-up resolutions

#### 10.1 Texture upload policy

- The guarded synchronous staging-buffer one-subresource `IDevice::WriteTexture()` path stays the **fail-closed correctness baseline**.
- Runtime / streaming uploads **must** use `RHI::ITransferQueue` (the canonical seam declared by [`GRAPHICS-026`](../../tasks/done/GRAPHICS-026-vulkan-renderer-plumbing-followups.md)), not the blocking graphics-queue helper.
- Per-subresource layout tracking stays whole-image for the batched path.
- Multi-mip / multi-layer / cubemap batching plus opt-in `gpu;vulkan` smoke landed in [`GRAPHICS-018T`](../../tasks/done/GRAPHICS-018T-texture-upload-batching.md), not in this clarification.

#### 10.2 Sampler anisotropy

- Stays expressed through the existing `RHI::SamplerDesc::MaxAnisotropy` float. No new RHI-visible enum or capability is added.
- The Vulkan backend probes `VkPhysicalDeviceFeatures::samplerAnisotropy` during physical-device selection alongside the existing required Vulkan 1.2 / 1.3 features, enables it on the logical device when supported, and records support / enablement on `GetVulkanBootstrapDiagnosticsSnapshot()`.
- At sampler creation, anisotropy is silently disabled when the feature is unsupported or `MaxAnisotropy <= 1.0`; otherwise it is clamped to `min(MaxAnisotropy, VkPhysicalDeviceLimits::maxSamplerAnisotropy)` with one warn breadcrumb when clamping reduces the value. Missing support never fails sampler creation.

#### 10.3 Fallback reason taxonomy

Each fail-closed counter and its reason enum stay **1:1** to its path:

- Future device-loss / extension / feature-negotiation reasons in the pipeline path are appended to the existing `FallbackPipelineReason` enum.
- Any second reason in another counter introduces a **new** path-local `FallbackXxxReason` enum named after that counter, with a matching `LastXxxReason` field appended to `FallbackDiagnosticsSnapshot` after the existing eight fields.

This keeps consumers from having to demultiplex a combined enum, and counter process-monotonicity stays independent of any reason field.

#### 10.4 Per-call vs frame-loop breadcrumbs

- **Per-call canonical** for bindless / transfer-queue / pipeline-creation fallback paths during pre-bring-up. Those callsites fire infrequently while non-operational and visibility helps catch accidental loops.
- **Frame-loop counters** keep the existing once-per-fail-closed-cycle rate-limited breadcrumb policy. `FallbackDiagnosticsSnapshot` carries the precise diagnostic regardless of breadcrumb suppression.
- `Resize` stays unrate-limited.
- Migration of any per-call counter to once-per-frame rate-limited breadcrumbs (with a cumulative-skipped count appended) is a **separate semantic task** scoped to that counter only, opened only when operational bring-up demonstrates many-per-frame fallback firing.

### 11. Misc bring-up artifacts

`assets/shaders/depth_prepass.vert` is the promoted depth-prepass shader source for opt-in SPIR-V smoke coverage. Callers must not special-case Vulkan in renderer code.

## Consequences

Positive:

- Renderer / runtime code stays Vulkan-agnostic — every gate is `IDevice::IsOperational()`, never a Vulkan diagnostic.
- Runtime never aborts because Vulkan was requested but unavailable — the fail-closed path always returns Null.
- Bring-up failures (volk init, instance, device, swapchain, features, services) are observable through CPU diagnostics without booting a Vulkan device.
- Async transfer is usable by `gpu;vulkan` smoke and streaming seams before `IsOperational()` is true, without `Extrinsic::Backends::Vulkan::*` leaks into runtime / renderer code.
- The `GRAPHICS-018Q` clarifications close out the open Vulkan integration questions with explicit, reviewable policies that do not require widening the RHI surface.

Trade-offs and risks:

- `GetTransferQueue()` returning the live queue while `IsOperational() == false` is an explicit asymmetry against `GetBindlessHeap()`; documented here so future readers do not "normalize" it without understanding the smoke-coverage motivation.
- The fail-closed → fail-closed → first-fire-breadcrumb pattern relies on per-counter rate limiting. Adding a new fail-closed path without wiring its breadcrumb through the rate limiter would regress the 60 Hz log-spam guarantee. ADR-0005 (operational readiness gate) records the gate; this ADR records the breadcrumb policy.
- The 1:1 counter ↔ reason-enum rule blocks any future "global fallback reason" demultiplex. That is intentional: process-monotonic counters must stay reorder-safe.

Follow-up tasks required: none. All `GRAPHICS-018Q` follow-ups are resolved and the parent retired tasks are referenced above.

## Alternatives Considered

- **Eager Vulkan-or-die initialization** (no Null fallback). Rejected: defeats the "default CPU/null correctness path" mandate; would force every host without a Vulkan-capable surface to fail startup or rebuild with an off-by-default flag, regressing CI parity.
- **Single global `FallbackReason` enum** across all counters. Rejected per §10.3: consumers would need to demultiplex against counter identity, and reordering would silently change reason semantics in serialized diagnostics.
- **Per-call breadcrumbs for frame-loop fail-closed paths.** Rejected per §6 / §10.4: spams 60 Hz logs while running on a host without a Vulkan device, which is the default CI configuration.
- **Eager batched-upload-only `IDevice::WriteTexture()` removal.** Rejected per §10.1: the synchronous path is the correctness baseline that the batched path is validated against; runtime / streaming code routes through `RHI::ITransferQueue` instead.

## Validation

- `tasks/done/GRAPHICS-018` records the guarded bring-up paths and the operational-boundary documentation.
- `tasks/done/GRAPHICS-018Q` records the four clarification resolutions captured in §10.
- `tasks/done/GRAPHICS-018T` records the multi-mip / multi-layer / cubemap batching path and the opt-in `gpu;vulkan` smoke.
- `tasks/done/GRAPHICS-018R` records the operational-transition reset seam (`IRenderer::RebuildOperationalResources()`).
- `tasks/done/GRAPHICS-026` records the canonical `RHI::ITransferQueue` seam used by §10.1.
- CPU diagnostics fixtures under `tests/contract/graphics/` and `tests/unit/graphics/` exercise the fail-closed return values and rate-limited breadcrumb policy without requiring a Vulkan device.
- The opt-in `gpu;vulkan` smoke targets selected via `ctest -L 'gpu' -L 'vulkan'` exercise the bring-up paths on a Vulkan-capable host.

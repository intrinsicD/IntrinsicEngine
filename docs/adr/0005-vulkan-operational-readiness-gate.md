# ADR 0005 — Vulkan Operational Readiness Gate and Runtime Reconciliation

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics / Vulkan backend, Runtime composition
- **Related tasks:** [`tasks/done/GRAPHICS-033`](../../tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md), [`GRAPHICS-033A`](../../tasks/done/GRAPHICS-033A-vulkan-operational-status-evaluator.md), [`GRAPHICS-033B`](../../tasks/done/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md), [`GRAPHICS-033C`](../../tasks/done/GRAPHICS-033C-vulkan-minimal-recipe-recording.md), [`GRAPHICS-033E`](../../tasks/done/GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md), [`GRAPHICS-033F`](../../tasks/done/GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/graphics/vulkan/README.md`](../../src/graphics/vulkan/README.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md)
- **Supersedes:** none. Extracted from the `## Vulkan operational readiness and runtime fallback` section in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/done/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0004](0004-vulkan-backend-bringup-and-fallback.md) records the bring-up sequence, `IsOperational()` predicate, diagnostics snapshots, and fail-closed breadcrumb policy that this gate sits on top of.

## Context

[ADR-0004](0004-vulkan-backend-bringup-and-fallback.md) records that the promoted Vulkan backend boots fail-closed: `VulkanDevice::Initialize()` can succeed and the backend can expose live services without the device ever becoming operational. Renderer and runtime code branch only on `RHI::IDevice::IsOperational()`; the question this ADR answers is **what that predicate is allowed to return `true` on**, and **how runtime reconciles a request for promoted Vulkan when the gate is not satisfied**.

Three forces shape the decision:

1. The 2026-05-08 sandbox visible-geometry review requires the backend to stay fail-closed until canonical renderer pass command recording, synchronization/barrier validation, queue-family ownership, and public service fallback reconciliation are demonstrably correct. Flipping `IsOperational()` earlier risks executing real GPU work against partially-initialized state.
2. The repository's default CPU correctness gate excludes `gpu|vulkan` labels. CI hosts and developer machines without a Vulkan device must continue to run the full default gate without aborts, and runtime must report *why* it fell back without forcing each consumer to demultiplex CMake options, runtime config flags, host capability probes, and per-service fallback counters.
3. Renderer code must remain Vulkan-agnostic. Operational state must be expressed through `RHI::IDevice::IsOperational()` and backend-neutral framegraph stats; the gate's evaluator and diagnostics live behind a Vulkan-public, non-native surface that runtime can consume without calling `vk*` symbols.

This ADR captures the gate enumeration, the single-source-of-truth evaluator, the status/reason taxonomy, the runtime reconciliation truth table, the validation-layer policy, the required-vs-optional capability split, and the rules for transient operational drops. It is the canonical durable home for the operational-readiness decision. `docs/architecture/graphics.md` retains a single pointer line to this ADR.

## Decision

### 1. Ordered operational gate

`RHI::IDevice::IsOperational()` may return `true` for the promoted Vulkan device only after this ordered, append-only checklist is satisfied. The list is backend-owned; future gates append new steps without renumbering existing ones.

1. **Build/runtime request reconciled.** `ExtrinsicBackendsVulkan` is compiled (build option `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON`), `RenderConfig::EnablePromotedVulkanDevice == true`, and the runtime asked for `GraphicsBackend::Vulkan` rather than Null.
2. **Instance + surface + physical device + queue families live.** volk, `VkInstance`, the platform surface, the selected `VkPhysicalDevice`, and the recorded graphics/present/transfer queue-family indices are present.
3. **Logical device + features + allocator + heap diagnostics live.** Logical device created with the required Vulkan 1.2/1.3 feature chain, `VK_KHR_swapchain`, VMA allocator initialized, and the documented heap-budget diagnostics populated.
4. **Swapchain create/acquire/present/resize/recreate satisfies the GRAPHICS-013CQ contract.** Explicit surface format, color space, and present-mode policy are in place, and device-lost handling routes through the documented recreate path.
5. **Per-frame command + sync objects live for the default recipe.** Per-frame command pools, primary command buffers, fences, binary acquire/render semaphores, and the transfer timeline semaphore path are all live.
6. **Default surface + present recording bodies match the CPU/null command contract.** The canonical default-recipe surface/present path executes against the real command context and produces the same pass/resource/command sequence as the CPU/null recipe contract.
7. **Barrier/layout validation reports no hard errors.** `SceneColorHDR` color-write → sampled, `SceneDepth` depth-attachment lifetime, imported backbuffer finalization, and transfer-queue uploads all pass GRAPHICS-022 structured findings without hard errors.
8. **Public services agree on the same operational answer.** Bindless heap, transfer queue, pipeline manager, swapchain/backbuffer import, and command context all report one consistent operational answer; no service is operational while another is in fallback.
9. **Validation-layer policy passes.** Validation-layer policy has run for the gate check and no validation error or required breadcrumb forces a fail-closed status.

Rejected alternative: flipping operational as soon as guarded acquire/submit/present works. That path can still disagree with renderer pass recording, render-graph barrier validation, or public service fallback state, and would bypass the 2026-05-08 review's fail-closed requirement.

### 2. Single source of truth: `EvaluateVulkanOperationalStatus(...)`

Operational state has exactly one evaluator, exported by `Extrinsic.Backends.Vulkan`:

```
EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) -> VulkanOperationalStatus
```

`VulkanDevice::IsOperational()` and runtime startup reconciliation consume that result; no renderer, runtime, or app code re-derives operational state from CMake options, config flags, bootstrap snapshots, or individual fallback counters. `VulkanOperationalInputs` is a Vulkan-public, non-native aggregate of booleans/reason bits; it contains no `Vk*` handles, so consumers compile without the Vulkan headers.

Rejected alternative: `Backends::Vulkan::IsOperational(const VulkanDeviceContext&) -> bool`. A bool-only API hides *why* the backend fell back and would force runtime to duplicate CMake/config/host-support checks to produce diagnostics.

### 3. Status and reason taxonomy

Status is an append-only enum, not a bool:

```
VulkanOperationalStatusCode {
  NotCompiled,
  NotRequested,
  RequestedButUnsupported,
  RequestedButFailedInit,
  RequestedButValidationFailed,
  RequestedButIncompleteGate,
  Operational,
}
```

A companion `VulkanOperationalReason` enum records the first failing gate, also append-only:

```
VulkanOperationalReason {
  MissingInstance,
  MissingSurface,
  NoSuitablePhysicalDevice,
  MissingRequiredExtension,
  MissingRequiredFeature,
  LogicalDeviceFailed,
  AllocatorFailed,
  SwapchainFailed,
  CommandSyncFailed,
  DefaultRecipeRecordingMissing,
  BarrierValidationFailed,
  PublicServiceReconciliationFailed,
  ValidationLayerError,
  DeviceLost,
  SurfaceLost,
}
```

Future feature gates (ray tracing, mesh shaders, work graphs, multi-GPU, …) append reasons; existing numeric values are not rewritten, and consumers do not need to demultiplex reason identity against counter identity.

Rejected alternative: one mega-enum shared with `FallbackPipelineReason`. [ADR-0004 §10.3](0004-vulkan-backend-bringup-and-fallback.md) already locks path-local fallback reason enums; operational status is a gate-level decision and stays separate from per-service fallback counters.

### 4. Runtime reconciliation truth table

Runtime maps `(CompiledIn, Requested, HostSupports, InitSucceeded, GateStatus)` to the effective device and startup diagnostic as follows. Runtime never aborts solely because requested Vulkan falls back to Null; aborts remain reserved for existing fatal platform/runtime initialization failures outside this gate.

| CompiledIn | Requested | HostSupports | InitSucceeded | GateStatus                       | Effective | Counter increments                                              | Warn breadcrumb                          | Runtime result |
|------------|-----------|--------------|---------------|----------------------------------|-----------|------------------------------------------------------------------|------------------------------------------|----------------|
| false      | false     | n/a          | n/a           | `NotRequested`                   | Null      | none                                                             | none                                     | continue       |
| false      | true      | n/a          | n/a           | `NotCompiled`                    | Null      | `VulkanFallbackToNullCount`                                      | `VulkanRequestedButNotOperational` once  | continue       |
| true       | false     | n/a          | n/a           | `NotRequested`                   | Null      | none                                                             | none                                     | continue       |
| true       | true      | false        | false         | `RequestedButUnsupported`        | Null      | `VulkanFallbackToNullCount`                                      | `VulkanRequestedButNotOperational` once  | continue       |
| true       | true      | true         | false         | `RequestedButFailedInit`         | Null      | `VulkanFallbackToNullCount`, `VulkanInitFailureCount`            | `VulkanRequestedButNotOperational` once  | continue       |
| true       | true      | true         | true          | `RequestedButValidationFailed`   | Null      | `VulkanFallbackToNullCount`, `VulkanValidationErrorCount`        | `VulkanRequestedButNotOperational` once  | continue       |
| true       | true      | true         | true          | `RequestedButIncompleteGate`     | Null      | `VulkanFallbackToNullCount`, `VulkanOperationalGateFailureCount` | `VulkanRequestedButNotOperational` once  | continue       |
| true       | true      | true         | true          | `Operational`                    | Vulkan    | none                                                             | none                                     | continue       |

Rejected alternative: aborting when Vulkan was explicitly requested but unsupported. The default CPU/null correctness path must remain available on CI and developer hosts without Vulkan.

### 5. Diagnostics surface

`VulkanOperationalDiagnosticsSnapshot` is exported by `Extrinsic.Backends.Vulkan`. It carries:

- Process-monotonic counters: `VulkanFallbackToNullCount`, `VulkanInitFailureCount`, `VulkanValidationErrorCount`, `VulkanOperationalGateFailureCount`, `VulkanDeviceLostOperationalDropCount`.
- A fixed-size histogram indexed by `VulkanOperationalReason`.

Runtime reads the snapshot at startup, after device reset/recreate attempts, and around false → true / true → false operational transitions. It emits exactly one startup `VulkanRequestedButNotOperational` warn breadcrumb per engine initialization attempt when `Requested == true` and the effective device is Null; the breadcrumb carries the `VulkanOperationalStatusCode` and the first failing `VulkanOperationalReason`.

Repeated frame-loop fail-closed events continue to use the [ADR-0004 §6](0004-vulkan-backend-bringup-and-fallback.md) rate-limited per-path breadcrumb policy. The startup breadcrumb is separate from those frame-loop breadcrumbs.

Renderer code still reads only `RHI::IDevice::IsOperational()` and backend-neutral `RenderGraphFrameStats`. Runtime may report backend status but must not call `vk*` symbols or branch on Vulkan-native types.

Rejected alternative: putting the counters on renderer diagnostics. Renderer diagnostics must stay backend-agnostic and available to Null; Vulkan operational reasons belong to the Vulkan backend plus the runtime startup surface.

### 6. Validation-layer policy

- Validation layers are requested in Debug, RelWithDebInfo, and CI preset builds when the host exposes `VK_LAYER_KHRONOS_validation`.
- Release requests no validation layer by default.
- Missing validation layers in a validation-enabled configuration **do not** fail the gate by themselves but record a warning reason.
- Any validation error emitted during operational-gate evaluation forces `RequestedButValidationFailed`, increments `VulkanValidationErrorCount`, and keeps the backend fail-closed until a later implementation slice proves the error is benign and updates the reason policy.

Rejected alternative: treating validation messages as logs only. The operational transition is the safety boundary; a known validation error during that boundary means the backend is not ready for default execution.

### 7. Required vs optional Vulkan capabilities

**Required** for the default operational recipe:

- Vulkan 1.3, or Vulkan 1.2 plus the promoted feature chain already used in [GRAPHICS-018](../../tasks/done/GRAPHICS-018-vulkan-renderer-integration.md).
- Platform surface extension and `VK_KHR_swapchain`.
- `synchronization2` / timeline-semaphore support used by existing transfer/frame paths.
- Dynamic rendering.
- Descriptor indexing with partially-bound + update-after-bind sampled-image arrays.
- Buffer device address.
- Supported depth/color attachment formats for `SceneDepth` and `SceneColorHDR`.
- Presentation support for the chosen queue/surface pair.

**Optional / probed** (recorded, never silently enables a feature outside the declared default recipe):

- Sampler anisotropy (governed by [ADR-0004 §10.2](0004-vulkan-backend-bringup-and-fallback.md)).
- Preferred mailbox / immediate present modes.
- Transfer-only queue family.
- Wider heap budgets.
- Debug-utils naming.
- Future ray tracing, mesh shader, work graph, and GPU decompression gates.

Rejected alternative: enabling every available extension at device creation. That makes tests host-dependent and hides which capability is actually required for visible geometry.

### 8. Queue-family ownership

- Prefer one graphics+present queue family.
- Use a distinct transfer family when available and beneficial.
- Swapchain images use the existing graphics/present sharing policy.
- Buffers/textures touched by both graphics and transfer queues keep the GRAPHICS-018T concurrent-sharing baseline until a later explicit ownership-transfer optimization task replaces it.
- The default recipe records graphics-queue commands for the canonical visible path; transfer-queue uploads must complete or expose valid timeline waits before the surface pass consumes uploaded resources.

Rejected alternative: requiring a dedicated transfer queue for operational status. Many valid hosts expose only a unified graphics/present/transfer family and the minimal visible-geometry path must support them.

### 9. Transient operational drops

Operational status may drop from `Operational` to a non-operational status on:

- Device loss.
- Surface loss.
- Swapchain recreation failure.
- Required service teardown.
- Validation-layer failure during resize/recreate.

Runtime must surface the transition through the same diagnostics surface and must not silently replace the already-created renderer/device pair with Null mid-frame. Recovery is explicit: wait idle when possible, recreate swapchain/services, re-evaluate the gate, then invoke the existing [GRAPHICS-018R](../../tasks/done/GRAPHICS-018R-operational-transition.md) `IRenderer::RebuildOperationalResources()` seam on false → true.

Rejected alternative: silently falling back to Null after a runtime device-loss event. That would mask data loss and invalidate renderer-owned GPU handles without a deterministic rebuild boundary.

### 10. Performance characteristics

The operational gate is evaluated at:

- Device initialization.
- Swapchain/device recreate.
- Explicit operational-transition checks.

It is **not** a per-frame decision. Per-frame code reads a cached atomic/status value plus the process-monotonic counters listed in §5. Diagnostics use enums and fixed-size histograms; nothing allocates strings per frame. Human-readable breadcrumb formatting happens only on transition/startup emission.

Rejected alternative: recomputing every gate item every frame. Adds CPU overhead and risks host-dependent log churn in the hot path.

### 11. Extensibility

Future gates append to the ordered checklist in §1 and to the reason enum in §3 without rewriting existing minimal-visible-geometry gates. Forecasted append-only gates: ray tracing (`GRAPHICS-045`), mesh shaders (`GRAPHICS-053`), work graphs (`GRAPHICS-054`), GPU decompression transfer (`GRAPHICS-057`), multi-GPU/device groups, HDR10/advanced present modes, async-compute/multi-queue scheduling (`GRAPHICS-037`). Each future task owns its required-vs-optional capability split and adds tests for its appended reason.

Rejected alternative: reserving broad placeholder reasons such as `AdvancedFeatureMissing`. Diagnostics must name the exact feature that blocked operational status.

### 12. Layering audit

- `src/graphics/vulkan/*` owns Vulkan-native probing, status evaluation inputs, and backend diagnostics; it imports no live ECS, runtime, app, or asset-service ownership.
- `src/runtime/*` owns config/CMake/device reconciliation and startup breadcrumb emission but does not call `vk*` symbols or inspect native handles.
- Renderer/pass code consumes only `RHI::IDevice::IsOperational()`, backend-neutral command stats, framegraph validation findings from GRAPHICS-022, and data snapshots.
- The app remains `runtime`-only.

Rejected alternative: letting renderer or app inspect Vulkan diagnostics to choose alternate passes. That creates a graphics/app backdoor around the RHI seam and would violate `AGENTS.md` §2 layering.

## Consequences

Positive:

- Renderer / runtime code stays Vulkan-agnostic — every gate is `IDevice::IsOperational()` or a backend-neutral framegraph finding, never a Vulkan diagnostic.
- Runtime never aborts because Vulkan was requested but unavailable — the truth table always falls back to Null with a single startup breadcrumb.
- Operational state has one evaluator and one snapshot surface; consumers cannot disagree on whether the backend is operational.
- The append-only checklist + reason enum lets future capability gates (ray tracing, mesh shaders, etc.) land without renumbering or invalidating diagnostics histograms.
- The default CPU/null correctness gate stays valid on every host because `Operational` is the only row in §4 that selects Vulkan.

Trade-offs and risks:

- The 9-step checklist is strict: a Vulkan-capable host that fails any one gate still falls back to Null. That is the intended safety boundary, but it means operational coverage requires every implementation child (Impl-A/B/C/D plus the [GRAPHICS-033E](../../tasks/done/GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md) and [GRAPHICS-033F](../../tasks/done/GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md) gate-input fills) to land before a host can flip to `Operational`.
- The append-only enum rule blocks renumbering, so adding a gate between two existing reasons is forbidden. Future readers must accept reason ordering that follows authorship date, not topical grouping.
- Runtime's startup breadcrumb fires once per engine initialization attempt. A consumer that restarts the engine many times per process will see one breadcrumb per restart; this is intentional (so each fallback is observable) but loud in test harnesses that re-initialize.

Follow-up tasks required: none. All planning children identified by `GRAPHICS-033` (Impl-A/B/C/D) are retired or in the active queue under their own task IDs, and the gate-input planning-gap fills `GRAPHICS-033E` / `GRAPHICS-033F` are retired.

## Alternatives Considered

- **Bool-only `IsOperational()` evaluator.** Rejected per §2: hides the failure reason and forces every consumer to duplicate CMake / config / host probes.
- **Mega-enum shared with `FallbackPipelineReason`.** Rejected per §3: collapses two independently-evolving taxonomies and would require renumbering when one side appends.
- **Abort on requested-but-unsupported Vulkan.** Rejected per §4: defeats the default CPU correctness path on Vulkan-less hosts.
- **Validation messages as logs only.** Rejected per §6: validation errors during gate evaluation are the safety boundary; treating them as informational allows known-broken paths to flip operational.
- **Enable every available Vulkan extension at device creation.** Rejected per §7: makes tests host-dependent and hides which capability blocks visible geometry.
- **Per-frame re-evaluation of the gate.** Rejected per §10: adds CPU overhead and risks log churn in the hot path; the gate is an initialization/recreate concern, not a per-frame one.
- **Reserved broad reasons such as `AdvancedFeatureMissing`.** Rejected per §11: diagnostics must name the exact feature that blocked operational status.
- **Renderer or app inspection of Vulkan diagnostics.** Rejected per §12: violates the RHI seam and `AGENTS.md` §2.

## Validation

- [`tasks/done/GRAPHICS-033`](../../tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md) records the 14 planning decisions captured above, including the gate checklist and reconciliation truth table.
- [`tasks/done/GRAPHICS-033A`](../../tasks/done/GRAPHICS-033A-vulkan-operational-status-evaluator.md) records the evaluator, status/reason enums, and CPU `contract;graphics` tests for gate order and the reconciliation matrix.
- [`tasks/done/GRAPHICS-033B`](../../tasks/done/GRAPHICS-033B-vulkan-operational-diagnostics-and-breadcrumb.md) records the diagnostics snapshot, counters, histogram, startup breadcrumb wiring, and `contract;runtime` requested-Vulkan → Null fallback tests.
- [`tasks/done/GRAPHICS-033C`](../../tasks/done/GRAPHICS-033C-vulkan-minimal-recipe-recording.md) recorded the bootstrap recording bodies that originally satisfied gate step 6; [`tasks/done/GRAPHICS-081`](../../tasks/done/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) retargeted the reason name and gate wording to the canonical default recipe after the bootstrap scaffold retired.
- [`tasks/done/GRAPHICS-033E`](../../tasks/done/GRAPHICS-033E-vulkan-operational-gate-barrier-validation.md) records the barrier-validation input feeding gate step 7.
- [`tasks/done/GRAPHICS-033F`](../../tasks/done/GRAPHICS-033F-vulkan-operational-gate-public-service-reconciliation.md) records the public-service reconciliation feeding gate step 8.
- The retired opt-in `gpu;vulkan` visible-triangle smoke ([`GRAPHICS-033D`](../../tasks/done/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md)) is the canonical end-to-end validation: it asserts `Operational` only after all 9 gate prerequisites are met and that no fallback counters increment during the operational frame.
- The default CPU gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the evaluator, reconciliation matrix, and startup breadcrumb without a Vulkan device.

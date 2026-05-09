# GRAPHICS-033 — Vulkan operational readiness and runtime fallback diagnostics (planning)

## Goal
Lock down the operational-readiness gate for the promoted Vulkan backend — the exact contract list, the single source of truth for "operational vs not", the runtime/CMake/device reconciliation matrix, the diagnostics surface, and the test split — before any code changes. The backend stays fail-closed in this slice; planning produces the gate definitions that GRAPHICS-018 / 018R / 026 implementation children consume.

## Non-goals
- No implementation, no Vulkan command-recording bodies, no CMake option additions in this slice.
- No new graphics passes, shaders, or materials (covered by GRAPHICS-031 / 032 and earlier).
- No optional Vulkan extension growth (mesh shaders, ray tracing, etc.) beyond the GRAPHICS-032 minimal recipe.
- No texture upload feature growth (GRAPHICS-018T / 026 own those).
- No live ECS access from `src/graphics/vulkan/*`.
- No bypass of fail-closed behavior; the backend remains fail-closed until contracts are demonstrably satisfied.
- No editor / ImGui present integration changes beyond what GRAPHICS-013CQ records.

## Context
- Owner layer: `graphics/vulkan` for the operational gates and recording bodies; `runtime` for reconciliation between reference engine config, CMake options, and device fallback.
- `src/runtime/Runtime.Engine.cpp` returns `Backends::Null::CreateNullDevice()` when promoted Vulkan is not both compiled and enabled. `src/runtime/Runtime.Engine.cppm` sets the reference render backend to Vulkan, but execution is gated by promoted-device configuration and CMake build options.
- `src/graphics/vulkan/README.md` documents the backend as fail-closed (`IsOperational() == false`) until canonical renderer pass command recording, synchronization/barrier validation, queue-family ownership, and service fallback reconciliation are completed.
- The 2026-05-08 review (sections "Exact missing pieces / 6" and "minimal milestone plan / 4") requires preserving fail-closed behavior until contracts are met and adding explicit diagnostics when null fallback is selected despite Vulkan being requested.
- GRAPHICS-018 / 018Q / 018R / 018S / 018T / 026 already establish integration scaffolding, sampler/border-color, texture-upload batching, and operational-transition planning.
- GRAPHICS-032 lands the minimal CPU-mock recipe that the operational Vulkan path must record against the real device once enabled.

## Recorded design decisions

Each decision below is locked for downstream implementation children. Trade-offs
are recorded so reviewers can see what was rejected and why.

1. **Operational gate enumeration.**
   - Decision: `RHI::IDevice::IsOperational()` may return `true` for the promoted Vulkan device only after this ordered checklist is satisfied:
     1. build/run gate reconciled (`ExtrinsicBackendsVulkan` compiled when requested, `RenderConfig::EnablePromotedVulkanDevice == true`, and the runtime asked for Vulkan rather than Null);
     2. volk, `VkInstance`, platform surface, selected `VkPhysicalDevice`, and recorded graphics/present/transfer queue-family indices are live;
     3. logical device, required Vulkan 1.2/1.3 feature chain, `VK_KHR_swapchain`, VMA allocator, and documented heap-budget diagnostics are live;
     4. swapchain create/acquire/present/resize/recreate paths satisfy the GRAPHICS-013CQ fullscreen-present contract, with explicit surface format/color-space/present-mode policy and device-lost handling;
     5. per-frame command pools, primary command buffers, fences, binary acquire/render semaphores, and the transfer timeline semaphore path are live for the GRAPHICS-032 minimal recipe;
     6. `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` recording bodies execute against the real command context and produce the same pass/resource/command sequence as the CPU/null recipe contract;
     7. barrier/layout-transition validation for `SceneColorHDR` color-write → sampled, `SceneDepth` depth attachment lifetime, imported backbuffer finalization, and transfer-queue uploads reports no hard errors through GRAPHICS-022 structured findings;
     8. public service fallback reconciliation is complete: bindless heap, transfer queue, pipeline manager, swapchain/backbuffer import, and command context all report one consistent operational answer;
     9. validation-layer policy has run for the gate check, and no validation error or required breadcrumb forces a fail-closed status.
   - Rejected alternative: flipping operational as soon as guarded acquire/submit/present works — rejected because that path can still disagree with renderer pass recording, render-graph barrier validation, or public service fallback state and would bypass the 2026-05-08 review's fail-closed requirement.

2. **Single source of truth.**
   - Decision: implementation child A introduces one graphics-public Vulkan diagnostic seam exported by `Extrinsic.Backends.Vulkan`: `EvaluateVulkanOperationalStatus(const VulkanOperationalInputs&) -> VulkanOperationalStatus`. `VulkanDevice::IsOperational()` and runtime reconciliation consume that result; no renderer, runtime, or app code re-derives operational state from CMake options, config flags, bootstrap snapshots, or individual fallback counters. `VulkanOperationalInputs` is a Vulkan-public, non-native aggregate of booleans/reason bits; it contains no `Vk*` handles.
   - Rejected alternative: `Backends::Vulkan::IsOperational(const VulkanDeviceContext&) -> bool` — rejected because a bool-only API hides why the backend fell back and would force runtime to duplicate CMake/config/host-support checks to produce diagnostics.

3. **Operational status and reason shape.**
   - Decision: status is an append-only enum, not a bool: `VulkanOperationalStatusCode { NotCompiled, NotRequested, RequestedButUnsupported, RequestedButFailedInit, RequestedButValidationFailed, RequestedButIncompleteGate, Operational }`. A companion `VulkanOperationalReason` enum records the first failing gate (`MissingInstance`, `MissingSurface`, `NoSuitablePhysicalDevice`, `MissingRequiredExtension`, `MissingRequiredFeature`, `LogicalDeviceFailed`, `AllocatorFailed`, `SwapchainFailed`, `CommandSyncFailed`, `MinimalRecipeRecordingMissing`, `BarrierValidationFailed`, `PublicServiceReconciliationFailed`, `ValidationLayerError`, `DeviceLost`, `SurfaceLost`). Future feature gates append reasons; existing numeric values are not rewritten.
   - Rejected alternative: one mega-enum shared with existing `FallbackPipelineReason` — rejected because GRAPHICS-018Q already locks path-local fallback reason enums. Operational status is a gate-level decision and remains separate from per-service fallback counters.

4. **Runtime reconciliation matrix.**
   - Decision: runtime maps `(CompiledIn, Requested, HostSupports, InitSucceeded, GateStatus)` to the effective device and startup diagnostic as follows:

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

     Runtime never aborts solely because requested Vulkan falls back to Null; aborts remain reserved for existing fatal platform/runtime initialization failures outside this gate.
   - Rejected alternative: aborting when Vulkan was explicitly requested but unsupported — rejected because the default CPU/null correctness path must remain available on CI and developer hosts without Vulkan.

5. **Diagnostic counters and breadcrumbs.**
   - Decision: implementation child B appends a `VulkanOperationalDiagnosticsSnapshot` exported by `Extrinsic.Backends.Vulkan` with process-monotonic counters `VulkanFallbackToNullCount`, `VulkanInitFailureCount`, `VulkanValidationErrorCount`, `VulkanOperationalGateFailureCount`, `VulkanDeviceLostOperationalDropCount`, and a fixed-size histogram indexed by `VulkanOperationalReason`. Runtime emits one startup warn breadcrumb named `VulkanRequestedButNotOperational` when `Requested == true` and the effective device is Null; that breadcrumb includes the `VulkanOperationalStatusCode` and first failing reason. Repeated frame-loop failures continue to use GRAPHICS-018Q rate-limited per-path breadcrumbs; the startup breadcrumb fires once per engine initialization attempt.
   - Rejected alternative: putting the counters on renderer diagnostics — rejected because renderer diagnostics must stay backend-agnostic and available to Null; Vulkan operational reasons belong to the Vulkan backend plus runtime startup surface.

6. **Validation-layer policy.**
   - Decision: validation layers are requested in Debug, RelWithDebInfo, and CI preset builds when the host exposes `VK_LAYER_KHRONOS_validation`; Release requests no validation layer by default. Missing validation layers in a validation-enabled configuration do not fail the gate by themselves but record a warning reason. Any validation error emitted during operational-gate evaluation forces `RequestedButValidationFailed`, increments `VulkanValidationErrorCount`, and keeps the backend fail-closed until a later implementation slice proves the error is benign and updates the reason policy.
   - Rejected alternative: treating validation messages as logs only — rejected because the operational transition is the safety boundary; a known validation error during that boundary means the backend is not ready for default execution.

7. **Required vs optional Vulkan capabilities.**
   - Decision: required instance/device support for the minimal operational recipe is: Vulkan 1.3 (or 1.2 plus the promoted feature chain already used in GRAPHICS-018), platform surface extension, `VK_KHR_swapchain`, synchronization2/timeline-semaphore support used by existing transfer/frame paths, dynamic rendering, descriptor indexing with partially-bound/update-after-bind sampled-image arrays, buffer device address, supported depth/color attachment formats for `SceneDepth` and `SceneColorHDR`, and presentation support for the chosen queue/surface pair. Optional/probed capabilities are sampler anisotropy (GRAPHICS-018Q), preferred mailbox/immediate present modes, transfer-only queue family, wider heap budgets, debug-utils naming, and future ray tracing / mesh shader / work graph / GPU decompression gates. Optional support is recorded but never silently enables a feature outside the declared minimal recipe.
   - Rejected alternative: enabling every available extension at device creation — rejected because it makes tests host-dependent and hides which capability is actually required for visible geometry.

8. **Queue-family ownership rules.**
   - Decision: prefer one graphics+present queue family; use a distinct transfer family when available and beneficial. Swapchain images use the existing graphics/present sharing policy; buffers/textures touched by both graphics and transfer queues keep the GRAPHICS-018T concurrent-sharing baseline until a later explicit ownership-transfer optimization task replaces it. The GRAPHICS-032 minimal recipe records graphics-queue commands only; transfer-queue uploads must complete or expose valid timeline waits before the surface pass consumes uploaded resources.
   - Rejected alternative: requiring a dedicated transfer queue for operational status — rejected because many valid hosts expose only a unified graphics/present/transfer family and the minimal visible-geometry path must support them.

9. **Diagnostic surface placement.**
   - Decision: operational diagnostics live in a new Vulkan backend snapshot (`VulkanOperationalDiagnosticsSnapshot`) plus a runtime startup summary that copies the effective status/reason into runtime logs. Runtime reads the snapshot at startup, after device reset/recreate attempts, and after a false → true / true → false operational transition. Renderer code still reads only `RHI::IDevice::IsOperational()` and backend-neutral `RenderGraphFrameStats`.
   - Rejected alternative: exposing `VkResult` or native handles through runtime — rejected by the `runtime` composition boundary; runtime may report backend status but must not call `vk*` or branch on Vulkan-native types.

10. **Hot-reload, swapchain recreation, and transient drops.**
    - Decision: operational status may drop from `Operational` to a non-operational status on device loss, surface loss, swapchain recreation failure, required service teardown, or validation-layer failure during resize/recreate. Runtime must surface the transition with the same diagnostics and must not silently replace the already-created renderer/device pair with Null mid-frame. Recovery is explicit: wait idle when possible, recreate swapchain/services, re-evaluate the gate, then invoke the existing GRAPHICS-018R `IRenderer::RebuildOperationalResources()` seam on false → true.
    - Rejected alternative: silently falling back to Null after a runtime device-loss event — rejected because it would mask data loss and invalidate renderer-owned GPU handles without a deterministic rebuild boundary.

11. **Test split.**
    - Decision: implementation children add CPU/default-gate tests labeled `contract;graphics` for the reconciliation matrix and first-failing-reason selection, `contract;graphics` mock validation-layer policy tests, and `contract;runtime` startup fallback emission tests that do not create a real Vulkan device. Opt-in `gpu;vulkan` smoke tests exercise a real-device GRAPHICS-032 visible-triangle frame only when a host supports Vulkan; they stay outside the default CPU gate per AGENTS.md §7.
    - Rejected alternative: making Vulkan smoke part of default CI — rejected because the repository's default CPU-supported correctness gate explicitly excludes `gpu|vulkan` labels.

12. **Performance characteristics.**
    - Decision: the operational gate is evaluated at device initialization, swapchain/device recreate, and explicit operational-transition checks only. It is not a per-frame decision; per-frame code reads a cached atomic/status value plus exact process-monotonic counters. Diagnostics use enums/fixed-size histograms and do not allocate strings per frame. Human-readable breadcrumb formatting happens only on transition/startup emission.
    - Rejected alternative: recomputing every gate item every frame — rejected because it adds CPU overhead and risks host-dependent log churn in the hot path.

13. **Extensibility forecast.**
    - Decision: future gates append to the ordered checklist and reason enum without rewriting existing minimal-visible-geometry gates. Forecasted append-only gates: ray tracing (`GRAPHICS-045`), mesh shaders (`GRAPHICS-053`), work graphs (`GRAPHICS-054`), GPU decompression transfer (`GRAPHICS-057`), multi-GPU/device groups, HDR10/advanced present modes, and async-compute/multi-queue scheduling (`GRAPHICS-037`). Each future task owns its required-vs-optional capability split and adds tests for its appended reason.
    - Rejected alternative: reserving broad placeholder reasons such as `AdvancedFeatureMissing` — rejected because diagnostics must name the exact feature that blocked operational status.

14. **Layering audit.**
    - Decision: `src/graphics/vulkan/*` owns Vulkan-native probing, status evaluation inputs, and backend diagnostics; it imports no live ECS, runtime, app, or asset-service ownership. `src/runtime/*` owns config/CMake/device reconciliation and startup breadcrumb emission but does not call `vk*` symbols or inspect native handles. Renderer/pass code consumes only `RHI::IDevice::IsOperational()`, backend-neutral command stats, framegraph validation findings from GRAPHICS-022, and data snapshots. The app remains `runtime`-only.
    - Rejected alternative: letting renderer or app inspect Vulkan diagnostics to choose alternate passes — rejected because it creates a graphics/app backdoor around the RHI seam and would violate AGENTS.md §2 layering.

## Required changes
- ✅ Recorded all fourteen decisions above, including the ordered gate checklist and the full runtime reconciliation truth table.
- ✅ Cross-linked with GRAPHICS-013CQ (present), GRAPHICS-018 / 018Q / 018R / 018S / 018T / 026, GRAPHICS-022 (rendergraph diagnostics), GRAPHICS-027 (post-shim cleanup), GRAPHICS-032 (minimal recipe consumer), and the [2026-05-08 sandbox visible-geometry review](../../docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md).
- ✅ Identified follow-up implementation children (do **not** open here):
  - **GRAPHICS-033-Impl-A** — add `VulkanOperationalStatusCode`, `VulkanOperationalReason`, `VulkanOperationalInputs`, `VulkanOperationalStatus`, `EvaluateVulkanOperationalStatus(...)`, and CPU `contract;graphics` tests for the gate order and reconciliation matrix. No runtime breadcrumb wiring and no command-recording bodies land here.
  - **GRAPHICS-033-Impl-B** — add `VulkanOperationalDiagnosticsSnapshot`, process-monotonic counters, reason histogram, `VulkanRequestedButNotOperational` startup breadcrumb wiring, and `contract;runtime` tests for requested-Vulkan → Null fallback diagnostics.
  - **GRAPHICS-033-Impl-C** — implement Vulkan command-recording bodies for the GRAPHICS-032 `FrameRecipe::MinimalDebugSurface` recipe once GRAPHICS-018R's operational-transition reset seam is available; add CPU command-sequence parity tests and keep real-device execution opt-in.
  - **GRAPHICS-033-Impl-D** — add the opt-in `gpu;vulkan` visible-triangle smoke fixture that exercises one GRAPHICS-032 frame on hosts with Vulkan support and asserts `Operational` only after all gate prerequisites are met.

## Tests
- Planning slice: validators only.
- Implementation children must add the matrix, diagnostic, and smoke tests as enumerated above.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```
- Optional GPU smoke gate (only on hosts with Vulkan):
  ```bash
  ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
  ```

## Docs
- Update `src/graphics/vulkan/README.md` to record the gate enumeration, reason enum, and diagnostic surface.
- Update `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md` with the operational-readiness gates and reconciliation rule.
- Update `docs/migration/nonlegacy-parity-matrix.md` rows for Vulkan operational status.
- Update `tasks/backlog/rendering/README.md` DAG after GRAPHICS-032.

## Acceptance criteria
- All fourteen decisions recorded with explicit answers and trade-off rationales; the gate checklist and the truth table are fully enumerated. ✅
- Implementation children identified with scope and dependency gates but not opened. ✅
- Backend remains fail-closed in this slice; no engine behavior changes land. ✅
- Layering invariants hold. ✅

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No relaxation of fail-closed behavior before the contracts are satisfied.
- No optional Vulkan extension growth beyond the minimal recipe.
- No live ECS access from Vulkan backend code.
- No texture-upload feature growth in this slice.
- No bool-only `IsOperational()` shape; reasons must be enumerable.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.

## Completion
- Completed: 2026-05-10.
- Commit reference: working-tree planning slice; replace with final commit SHA/PR reference before merge.
- Notes:
  - Decisions are mirrored in `src/graphics/vulkan/README.md`, `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, `docs/migration/nonlegacy-parity-matrix.md`, and `tasks/backlog/rendering/README.md`.
  - `GRAPHICS-033-Impl-A/B/C/D` are identified but explicitly **not** opened. Impl-A is the first implementation child; Impl-B depends on Impl-A's status/reason surface; Impl-C depends on GRAPHICS-032 implementation and the GRAPHICS-018R transition seam; Impl-D depends on Impl-C and remains `gpu;vulkan` opt-in.
  - No C++ code, CMake option, shader, runtime behavior, or Vulkan command-recording body landed in this planning slice.

# GRAPHICS-018 — Vulkan renderer integration

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-017` completion.
- Current slice:
  - Renderer frame lifecycle delegates `BeginFrame`/`EndFrame` and backbuffer import through `RHI::IDevice`.
  - Render-graph execution is bracketed by the frame graphics command context.
  - `CullingPass` and `DepthPrepass` command bodies are routed through backend-neutral renderer/RHI seams and soft-skip unavailable pipelines/resources with structured CPU/mock diagnostics.
  - `RenderGraphFrameStats` reports focused compile, execute, and name-keyed command-recording status.
  - Promoted Vulkan `IDevice` lifecycle/services/resource overrides are symbol-complete and fail-closed until full device/swapchain/resource bring-up lands.
  - Guarded Vulkan texture allocation/view/upload and `VkSampler` creation paths exist for future operational devices.
  - Fail-closed CPU diagnostics: `FallbackTransferQueue` upload paths and `VulkanDevice::CreatePipeline` increment process-monotonic counters (`GetFallbackTransferUploadAttemptCount()`, `GetFallbackPipelineCreationAttemptCount()`) and log structured breadcrumbs, mirroring the existing `GetFallbackBindlessAllocationAttemptCount()` pattern. Covered by `Test.VulkanFailClosedContract.cpp` so CPU CI surfaces accidental upload/pipeline traffic against non-operational devices.
  - Fail-closed `CreatePipeline` additionally exposes a structured `FallbackPipelineReason` enum (`None`, `PreBringUp`, `ShaderMissing`) via `GetLastFallbackPipelineReason()`, distinguishing "device/global pipeline layout not yet brought up" from "operational guard reached but shader/pipeline construction unimplemented". The `PreBringUp` reason is asserted by the CPU contract test; `ShaderMissing` is reachable only once operational bring-up lands and is locked in by `static_assert` in the contract test. Bindless and transfer-queue counters intentionally do not yet expose a reason enum because each has a single fail-closed reason today.
  - Process-monotonic accumulation of all three fail-closed counters (`GetFallbackBindlessAllocationAttemptCount`, `GetFallbackTransferUploadAttemptCount`, `GetFallbackPipelineCreationAttemptCount`) and persistence of `GetLastFallbackPipelineReason()` across `VulkanDevice::Initialize`/`Shutdown` cycles and across `VulkanDevice` instance destruction/re-creation are locked in by the CPU contract test `VulkanFailClosedContract.FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles`, so a future refactor cannot silently demote them to instance-scoped state without breaking the gate.
  - All three fail-closed counters and `GetLastFallbackPipelineReason()` are also exposed as a single `FallbackDiagnosticsSnapshot` aggregate via `GetFallbackDiagnosticsSnapshot()`, so CPU diagnostics consumers can read all four fields with one call instead of combining four independent free-function loads. The aggregate read is not tear-free across fields (each field is loaded with relaxed atomics in a fixed order: bindless, transfer, pipeline-count, last-pipeline-reason); single-threaded CPU contract coverage in `Test.VulkanFailClosedContract.cpp` (`FallbackDiagnosticsSnapshotMatchesIndividualGetters`, `FallbackDiagnosticsSnapshotIsProcessMonotonic`) asserts agreement with the individual getters and process-monotonic delta semantics.
  - `VulkanDevice::BeginFrame` on a non-operational device (or before swapchain bring-up) increments a process-monotonic `GetFallbackBeginFrameAttemptCount()` counter and emits a structured log breadcrumb, mirroring the bindless/transfer/pipeline counter pattern. The counter is also exposed as `FallbackDiagnosticsSnapshot::BeginFrameAttempts` (loaded last in the snapshot field order: bindless, transfer, pipeline-count, last-pipeline-reason, begin-frame-count). CPU coverage: `VulkanFailClosedContract.BeginFrameOnNonOperationalDeviceIncrementsAttemptCounter` asserts the counter increments per call; `FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles`, `FallbackDiagnosticsSnapshotMatchesIndividualGetters`, and `FallbackDiagnosticsSnapshotIsProcessMonotonic` extend to cover the new field across init/shutdown and through the snapshot accessor. This catches accidental runtime/renderer frame loops driving a fail-closed Vulkan device on CPU CI.
  - `VulkanDevice::EndFrame` on a non-operational device increments a process-monotonic `GetFallbackEndFrameAttemptCount()` counter and emits a structured log breadcrumb, mirroring the bindless/transfer/pipeline/begin-frame counter pattern. The counter is also exposed as `FallbackDiagnosticsSnapshot::EndFrameAttempts`, appended after `BeginFrameAttempts` in the snapshot field order (bindless, transfer, pipeline-count, last-pipeline-reason, begin-frame-count, end-frame-count). CPU coverage: `VulkanFailClosedContract.EndFrameOnNonOperationalDeviceIncrementsAttemptCounter` asserts the counter increments per call; `BeginEndFrameCountersTrackPairwiseOnNonOperationalDevice` asserts that paired `BeginFrame`/`EndFrame` calls advance both counters in lockstep, locking in the frame-loop pair invariant; `FallbackCountersAreProcessMonotonicAcrossInitializeShutdownCycles`, `FallbackDiagnosticsSnapshotMatchesIndividualGetters`, and `FallbackDiagnosticsSnapshotIsProcessMonotonic` extend to cover the new field across init/shutdown and through the snapshot accessor. Together with `BeginFrameAttempts`, this completes the frame-loop fail-closed pair so CPU CI surfaces both halves of an unbalanced renderer/runtime frame loop driving a fail-closed Vulkan device.
  - Live backend resource pools are drained during shutdown.
  - Runtime now owns a CPU-tested operational-transition check that waits idle and calls `IRenderer::RebuildOperationalResources()` when a fail-closed device becomes operational; the renderer rebuilds material GPU buffers, `GpuWorld` scene bindings, culling output resources, and the depth-prepass pipeline through RHI seams.
  - Runtime can select the promoted fail-closed Vulkan `IDevice` only when both `RenderConfig::EnablePromotedVulkanDevice` and `INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON` are enabled; default Vulkan config still uses the Null fallback.
  - Keep Vulkan opt-in and preserve the CPU/null correctness gate.
- Nonblocking questions: tracked in `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md`.
- Completed prerequisite: `tasks/done/GRAPHICS-018R-operational-transition.md`.
- Remaining blockers before marking Vulkan operational: real swapchain/surface/device bring-up, concrete pipeline creation, presentation diagnostics, and reconciliation of fail-closed fallback bindless/transfer behavior.
- Temporary fail-closed shim removal/reconciliation timeline:
  - Fallback bindless heap and fallback transfer queue: reconcile/remove in a future `GRAPHICS-018` operational bring-up slice before `VulkanDevice::IsOperational()` can become true.
  - Empty-handle `Create*` paths for non-operational devices: keep as fail-closed guards; operational-path replacement is owned by `GRAPHICS-018` bring-up, now able to call the renderer reset seam completed by `GRAPHICS-018R`.
  - Hard-coded sampler border color: `tasks/backlog/rendering/GRAPHICS-018S-sampler-border-color.md`, before non-black border colors are relied on by renderer/material behavior.
  - One-subresource blocking texture upload path: `tasks/backlog/rendering/GRAPHICS-018T-texture-upload-batching.md`, before multi-mip/layer Vulkan texture smoke tests.

## Goal
- Wire concrete Vulkan backend execution behind the promoted graphics interfaces and default frame recipe.
## Non-goals
- No CPU-only contract behavior changes that require Vulkan.
- No mandatory GPU/Vulkan tests in the default CPU gate.
- No platform-window ownership move into renderer passes.
## Context
- Owner: `src/graphics/vulkan`, `src/graphics/rhi`, `src/graphics/framegraph`, and renderer backend wiring.
- CPU/null contracts should be stable before Vulkan execution is tightened.
## Required changes
- Connect Vulkan device, swapchain, surface, command context, descriptors, pipelines, barriers, transfer, and profiler support to canonical frame execution.
- Implement Vulkan descriptor/scene-table integration for renderable-instance, transform, bounds/culling, material, geometry, and light SSBOs.
- Add resize, swapchain recreation, presentation, and backend-failure diagnostics.
- Keep backend-specific code behind RHI/backend seams.
## Tests
- Add opt-in `gpu`/`vulkan` tests for device creation, swapchain lifecycle, command recording, barriers, transfer, resize, and presentation where environment support exists.
- Add opt-in Vulkan smoke coverage for scene-table descriptor binding and GPU-driven culling/draw-bucket execution.
- Preserve CPU/null tests for all backend-independent behavior.
- Label Vulkan/GPU smoke tests `gpu;vulkan` so they remain opt-in; keep backend-independent CPU tests under `unit;graphics` or `contract;graphics` so they continue to run in the default CPU gate.
## Docs
- Document GPU verification requirements, headless/fallback behavior, and Vulkan label policy.
## Acceptance criteria
- Vulkan backend can execute the canonical frame recipe when opt-in GPU tests are enabled.
- CPU-supported tests remain independent of Vulkan availability.
- Backend failures are surfaced through structured diagnostics.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R '^RendererRhiBoundary\.' --timeout 60

# Optional non-headless Vulkan backend sanity check. Use only a configured build tree
# whose C++23 compiler/toolchain has been confirmed current; do not use stale trees
# with older compilers as verification evidence.
compiler=$(cmake -LA -N build/dev-clang-ninja | sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p')
"$compiler" --version | head -n 1
set -o pipefail
cmake --build build/dev-clang-ninja --target ExtrinsicBackendsVulkan -j2 2>&1 | tee /tmp/intrinsic-vulkan-backend-build.log | tail -n 120

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Optional when hardware/driver support is available:
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making Vulkan mandatory for normal CI or CPU correctness gates.

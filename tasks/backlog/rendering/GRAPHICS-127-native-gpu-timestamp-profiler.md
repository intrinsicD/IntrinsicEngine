---
id: GRAPHICS-127
theme: B
depends_on:
  - GRAPHICS-033
  - GRAPHICS-037D
  - GRAPHICS-119
  - RUNTIME-181
  - RUNTIME-182
maturity_target: Operational
---
# GRAPHICS-127 — Native GPU timestamp profiler and frame-recipe timing integration

## Goal
- Make the existing `Extrinsic.RHI.Profiler` seam an honest, nonblocking native-Vulkan feature that timestamps actual compiled frame-recipe passes and exposes the last resolved frame through existing telemetry and the Sandbox Frame Graph panel.

## Non-goals
- No new profiler service, interface, registry, database, trace exporter, or diagnostics window.
- No Tracy/Perfetto integration, pipeline-statistics queries, calibrated CPU/GPU clocks, timestamp SLOs, or wall-clock performance claims.
- No summing overlapping queue durations or inventing a calibrated cross-queue frame envelope.
- No default-gate GPU requirement and no CPU-clock substitution presented as GPU evidence.
- No barrier/wireframe/profiler architecture port from `Test`; IntrinsicEngine's RHI, frame graph, telemetry, and editor seams remain authoritative.

## Context
- Owners: `src/graphics/rhi`, `src/graphics/vulkan`, `src/graphics/renderer`, the existing runtime editor facade, and existing core telemetry. No live ECS ownership moves into graphics.
- `VulkanDevice::m_Profiler` is publicly exposed by `GetProfiler()` but never constructed. `VulkanProfiler::EndFrame()` emits no end timestamp, and `SetCommandBuffer`/`ResetFrame` have no production callers.
- `Resolve` mixes cyclic slot identity with monotonically increasing frame identity and compares absolute query indices with slot-local counts. Scope handle zero also doubles as failure.
- The Null profiler currently writes `steady_clock` CPU durations into fields named `Gpu*`; this is useful contract timing but misleading provenance.
- There are no production `BeginFrame`/scope/end/resolve calls. Existing `Core.Telemetry` pass-timing storage, `Graphics::RenderGraphFrameStats`, and the Sandbox Frame Graph model/window are the consumers; reuse them.
- `Test/engine/rendering/src/gpu_profiler.cpp` and its focused test supply useful lifecycle/test-case ideas only. Do not port its CPU-clock fallback or parallel architecture; IntrinsicEngine's native query, provenance, and compiled-pass contracts are stricter.
- Satisfied `GRAPHICS-033`, `GRAPHICS-037D`, and `GRAPHICS-119` are the operational Vulkan, multi-queue, and parallel pass-recording contracts this integration must respect.
- ADR-0027 places live config apply in the app-composed ConfigControl owner and
  editor presentation in the optional EditorUi owner. `RUNTIME-181` and
  `RUNTIME-182` therefore land first so this task adds the profiling flag and
  Frame Graph presentation through their narrow service/contribution seams
  rather than recreating `Engine::GetConfigControl()` or an ImGui facade.

## Control surfaces
- Config: add `RenderConfig::EnableGpuProfiling`, default `false`, with parse/serialize/round-trip and completed-frame-boundary hot-apply through the existing config-control lane. The flag gates recording/publication, never device resource lifetime.
- UI: the existing Frame Graph panel may toggle the same runtime-owned config path and display status/timings; do not add a second editor-owned flag.
- Agent/CLI: use the existing engine-config control facade for the same field.

## Backends
- Backend axis: native Vulkan timestamps are `Operational`; Null/mock backends are lifecycle/provenance `CPUContracted` only.
- Unsupported devices expose a truthful disabled reason and never synthesize native GPU evidence.

## Slice plan

1. **RHI lifecycle, provenance, and control (`CPUContracted`).** Repair the existing profiler value/error contract, make Null/mock results provenance-honest, add the one config field plus preview/round-trip/frame-boundary hot-apply path, and pin lifecycle/config/editor-model behavior with CPU tests. Supported profiler resources are device-lifetime objects; the toggle controls only new recording/publication. Defer all native-duration claims.
2. **Native Vulkan query lifecycle.** Construct the existing Vulkan adapter, bind query reset/write/resolve to real frame slots and command buffers, handle valid bits/period/capacity/device loss without waits, and pass focused conversion plus Vulkan lifecycle coverage. Defer compiled-pass publication until slot reuse is proven.
3. **Compiled-pass integration and presentation.** Instrument actual serial/parallel and graphics/async-compute pass recording, correlate resolved queue/pass rows into existing telemetry and `RenderGraphFrameStats`, and display them in the existing Frame Graph model/window. No new profiler or presentation module may appear.
4. **Operational proof and docs.** Run the non-skipped native smoke for more than `framesInFlight`, prove named-pass timing and truthful unsupported behavior, then land current-state RHI/Vulkan/renderer/editor documentation. Only this slice reaches the task's final `Operational` maturity.

Each slice is a separately reviewable commit and must pass its focused CPU checks plus all earlier slice gates. Later slices may not weaken or replace earlier provenance/nonblocking contracts.

## Required changes
- [ ] Repair the existing RHI contract with explicit monotonic frame identity separate from frame slot, a typed invalid scope result, queue/source provenance, and deterministic invalid-pairing/overflow/unsupported diagnostics.
- [ ] Make Null/mock profiling validate lifecycle, names, frame-slot reuse, and not-ready behavior without labeling host-clock durations as native GPU time.
- [ ] Add the one default-off config field and route config, editor, and agent/CLI writes through the canonical runtime config-control path.
- [ ] Resolve config apply and Frame Graph presentation through the
      app-composed owners delivered by `RUNTIME-181`/`RUNTIME-182`; do not add
      a profiling-specific Engine getter, callback, or UI path.
- [ ] Construct device-lifetime Vulkan profiler/query-pool resources during device initialization whenever native timestamps are supported. `EnableGpuProfiling` gates new recording/resolution/publication at a completed frame boundary; hot disable drains or retains the last resolved sample and never destroys/recreates pools while frames are in flight.
- [ ] Bind resets and timestamp writes to the real graphics/compute command buffers. Reset a reused slot only after its fence/completion proof, emit frame begin/end, and keep per-slot absolute/local query indexing coherent.
- [ ] Respect queue-family `timestampValidBits`, device timestamp period, query exhaustion, device loss, and command-buffer lifetime. Resolution must never use `VK_QUERY_RESULT_WAIT_BIT` or otherwise block the frame.
- [ ] Resolve only completed older frames before slot reuse and retain the last good resolved sample when the newest result is not ready.
- [ ] Bracket actual compiled pass command recording by stable compiled pass name/ID and resolved queue, including serial, parallel-recorded, graphics, and async-compute paths without data races.
- [ ] Publish native frame/pass timings, source/queue provenance, and a frame-correlation number into existing `Core.Telemetry`, `RenderGraphFrameStats`, and the existing Frame Graph editor data model.
- [ ] Give multi-queue frame timing an honest documented meaning (for example, a named per-queue envelope); do not sum overlapping work into a fake total.

## Tests
- [ ] CPU contract tests cover frame identity versus slot reuse, invalid begin/end pairing, invalid handles, scope exhaustion, not-ready retention, queue/source provenance, and truthful Null behavior.
- [ ] Add pure conversion tests for timestamp-period scaling, valid-bit masking/wrap handling, absolute/local query indices, and overflow guards.
- [ ] Add config parse/round-trip/frame-boundary hot-enable/hot-disable and Frame Graph model-copy tests proving one control surface, fixed device-resource lifetime, and one existing presentation path.
- [ ] Add an opt-in `gpu;vulkan` smoke that runs more than `framesInFlight`, resolves without waiting, and observes finite nonzero `NativeGpu` timing for at least one known named default-recipe pass.
- [ ] Exercise the parallel-recording path and, when async compute is enabled/supported, assert queue attribution without overlapping-duration summation.
- [ ] Unsupported timestamp hardware must produce a non-skipped truthful disabled result rather than CPU substitution.

## Docs
- [ ] Update the RHI profiler contract and `src/graphics/renderer/README.md` with lifecycle, provenance, control, and nonblocking semantics.
- [ ] Update `src/graphics/vulkan/README.md` with query-slot/fence ownership and the operational smoke.
- [ ] Update the current-state graphics architecture and Sandbox Frame Graph documentation; regenerate the module inventory if `.cppm` surfaces change.
- [ ] Add this leaf to the rendering/global backlog and as a readiness dependency of `REVIEW-003`.

## Acceptance criteria
- [ ] Enabling profiling on a supported Vulkan device produces correlated native pass timings from actual command recording after more than one slot-reuse cycle.
- [ ] The steady-state render path never waits for timestamp results.
- [ ] Null/unsupported data cannot be mistaken for native GPU evidence.
- [ ] Parallel and multi-queue recording remain race-free and retain truthful queue attribution.
- [ ] The existing Frame Graph panel shows last-resolved status/timings without a new service or window.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsRhiCpuUnitTests IntrinsicGraphicsRendererCpuUnitTests IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Profiler|Telemetry|RenderGraph|SandboxEditor' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'Profiler|Timestamp' --no-tests=error --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Blocking query resolution or using `VK_QUERY_RESULT_WAIT_BIT` in the frame path.
- Destroying or recreating profiler/query-pool resources in response to a hot toggle while frames may reference them.
- Reporting host-clock or summed-overlap durations as native GPU frame time.
- Adding another profiler abstraction, telemetry store, config flag, or editor window.
- Reintroducing an Engine domain facade or bypassing the shared validated
  config/UI contribution paths to wire profiling.
- Instrumenting CPU executor callbacks instead of the command-recording scopes they schedule.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Operational evidence requires a non-skipped native-Vulkan smoke with named pass timing and slot reuse; CPU/Null lifecycle tests alone are insufficient.

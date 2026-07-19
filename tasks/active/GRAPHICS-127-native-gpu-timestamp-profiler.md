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

## Status

- In progress as of 2026-07-19; owner: Codex team; implementation branch:
  `codex/graphics-127-gpu-profiler`.
- All five planned slices are implemented on the implementation branch. The
  RHI lifecycle checkpoint is CPU-contracted:
  the repaired provenance contract, truthful Null adapter, checked timestamp
  helpers, and focused CPU coverage are complete. The native Vulkan checkpoint
  now owns a fixed device-lifetime query pool, resolves submitted metadata only
  after the reused slot's existing fence proof, and degrades unsupported or
  failed timestamp setup without affecting device operation. The graphics-only
  compiled-pass checkpoint now preplans immutable scopes for accepted
  serial/parallel and graphics/async lanes, submits only globally advanced
  candidates, resolves exact reused-slot keys, publishes fresh recorded native
  rows to existing telemetry, and retains detailed last-good frame stats as
  stale on every nonfresh path. The config/control checkpoint now owns a
  default-off round-tripped hot field, synchronous Editor/AgentCli apply,
  one post-`UiEndCapture` immutable frame sample, and the existing Frame Graph
  panel's config and full profile projection. CPU contracts and current-state
  cross-layer docs cover that slice.
- A pre-audit operational checkpoint ran non-skipped on a host exposing an
  NVIDIA GeForce RTX 3050 with driver 590.48.01. Its evidence is superseded by
  the results-audit fixes for multi-wrap ambiguity, selected-device binding,
  device-loss propagation, and per-frame unsupported queues. The task remains
  active pending the held post-audit CPU/Vulkan verification and final
  integration review;
  this evidence slice does not retire it.
- `RUNTIME-181` and `RUNTIME-182` are retired, so ConfigControl and EditorUi
  are settled owners, not future blockers.
- The 2026-07-19 activation audit found the exported profiler seam unused in
  production: the Vulkan adapter was not constructed, `EndFrame()` did not
  write its closing timestamp, and the Null adapter reported host-clock values
  under `Gpu*` names. The completed implementation checkpoints repair that
  seam in place; this task does not add a parallel profiling architecture.

## Goal

- Make the existing `Extrinsic.RHI.Profiler` seam an honest, nonblocking native-Vulkan feature that timestamps actual compiled frame-recipe passes and exposes the last resolved frame through existing telemetry and the Sandbox Frame Graph panel.

## Non-goals

- No new profiler service, interface, registry, database, trace exporter, or diagnostics window.
- No Tracy/Perfetto integration, pipeline-statistics queries, calibrated CPU/GPU clocks, timestamp SLOs, or wall-clock performance claims.
- No summing overlapping queue durations or inventing a calibrated cross-queue frame envelope.
- No transfer-queue timestamp support in this slice. The promoted frame graph
  currently records profiled work only on graphics and async-compute queues;
  a future transfer path must prove legal timestamp/reset support for its
  actual queue family before extending this contract.
- No default-gate GPU requirement and no CPU-clock substitution presented as GPU evidence.
- No barrier/wireframe/profiler architecture port from `Test`; IntrinsicEngine's RHI, frame graph, telemetry, and editor seams remain authoritative.
- No profiler-owned thread pool, recording mutex, per-pass dynamic query
  allocation, or virtual timestamp methods added to every
  `RHI::ICommandContext` implementation.

## Context

- Owners: `src/graphics/rhi`, `src/graphics/vulkan`, `src/graphics/renderer`, the existing runtime editor facade, and existing core telemetry. No live ECS ownership moves into graphics.
- `VulkanDevice::m_Profiler` is publicly exposed by `GetProfiler()` but never constructed. `VulkanProfiler::EndFrame()` emits no end timestamp, and `SetCommandBuffer`/`ResetFrame` have no production callers.
- `Resolve` mixes cyclic slot identity with monotonically increasing frame identity and compares absolute query indices with slot-local counts. Scope handle zero also doubles as failure.
- The Null profiler currently writes `steady_clock` CPU durations into fields named `Gpu*`; this is useful contract timing but misleading provenance.
- There are no production `BeginFrame`/scope/end/resolve calls. Existing `Core.Telemetry` pass-timing storage, `Graphics::RenderGraphFrameStats`, and the Sandbox Frame Graph model/window are the consumers; reuse them.
- `Test/engine/rendering/src/gpu_profiler.cpp` and its focused test supply useful lifecycle/test-case ideas only. Do not port its CPU-clock fallback or parallel architecture; IntrinsicEngine's native query, provenance, and compiled-pass contracts are stricter.
- Satisfied `GRAPHICS-033`, `GRAPHICS-037D`, and `GRAPHICS-119` are the operational Vulkan, multi-queue, and parallel pass-recording contracts this integration must respect.
- `VulkanDevice::BeginFrame()` already waits the graphics, async-compute, and
  transfer fences for the reused frame slot before resetting its per-slot
  command resources. That existing wait is the completion proof: resolve the
  prior submitted profile after it and before any query reset. Do not add a
  query wait, fence wait, queue idle, or device idle for profiling.
- Serial, parallel, and multi-queue renderer lanes all converge on the same
  compiled-pass callback. The profiled interval is the GPU command sequence
  immediately around `recordPass(actualContext, passIndex)`; compiler work,
  inter-pass barrier packets, queue waits, submission, presentation, and CPU
  executor time are outside the measurement.
- ADR-0027 and retired `RUNTIME-181` place live config preview/validation and
  synchronous commit in the app-composed `EngineConfigControl`. Retired
  `RUNTIME-182` places presentation in app-owned `EditorUiHost`; the Sandbox
  `EditorShell` already owns the one Frame Graph contribution.

## Control surfaces

- Config: add `RenderConfig::EnableGpuProfiling`, JSON key
  `render.enable_gpu_profiling`, default `false`, and include it in the
  existing parse/serialize/round-trip and hot subset. Preview/validate/commit
  is synchronous. After `UiEndCapture`, `Engine::RunFrame()` samples the
  committed value once into `Graphics::RenderFrameInput`; that immutable value
  controls the acquired renderer frame. There is no completed-frame apply
  queue.
- UI: extend the existing Sandbox Frame Graph panel. Its toggle is enabled
  only when `SandboxEditorContext::EngineConfigCommandsAvailable` and both
  existing preview/apply callbacks are present. It edits a serialized active
  config candidate and invokes the same preview then
  `ApplyEngineConfigHotSubset(..., RuntimeConfigControlSource::Editor)` path;
  rejection leaves committed state unchanged and displays the existing
  diagnostics.
- Agent/CLI: resolve the existing `EngineConfigControl` service and use the
  same preview/apply path with `RuntimeConfigControlSource::AgentCli`.
- Omitted optional owners: without ConfigControl commands the panel is
  read-only; without EditorUi the renderer still honors boot/programmatic
  config; neither omission may crash or create a second control path.

## Backends

- Backend axis: native Vulkan timestamps are `Operational`; Null/mock backends are lifecycle/provenance `CPUContracted` only.
- Unsupported devices expose a truthful disabled reason and never synthesize native GPU evidence.

## Profiler data contract

- Keep and repair `Extrinsic.RHI.Profiler`. Use plain value types on the
  existing `IProfiler`: a frame key containing monotonically increasing
  submitted-frame number plus cyclic frame slot, a typed scope token with a
  distinct invalid value, immutable planned-scope descriptors, status/source
  provenance, and per-scope/per-queue results. RHI descriptors carry only
  ordinal, name, and resolved `RHI::QueueAffinity`; the renderer joins the
  ordinal back to `Graphics::FramePassId`.
- Replace ambiguous always-present `GpuFrameTimeNs`/`CpuSubmitTimeNs` values
  with explicit availability. At minimum, results distinguish
  `NativeGpu`, `ContractOnly`, and unavailable sources, and distinguish
  disabled, unsupported, priming/not-ready, resolved, exhausted/overflow,
  invalid lifecycle, and device-lost states. A zero duration is data only when
  a native query pair was available; it is never the unsupported sentinel.
- Plan query tokens deterministically on the caller thread after actual queue
  resolution and before worker fan-out. Worker recording consumes immutable
  tokens and borrowed actual command contexts. Replace the adapter's mutable
  `SetCommandBuffer`/linear-scan allocation; do not add per-pass locking or
  widen `ICommandContext` with profiler virtuals.
- A candidate frame becomes resolvable only after successful submission. A
  failed/discarded recording or submit is marked discarded, never published,
  and its range is reset before later reuse. Use the existing pre-submit
  `IDevice::GetGlobalFrameNumber()` plus `FrameHandle::FrameIndex` as the
  candidate key. Mark it submitted only when `EndFrame` advances the global
  number past that candidate; otherwise mark it discarded.

## Native Vulkan lifecycle

- Construct one device-lifetime Vulkan profiler adapter after successful
  Vulkan device initialization even when config is off or timestamp support is
  absent, so `GetProfiler()` can report explicit status. Allocate its query
  pool once only when at least one promoted actual queue supports timestamps,
  for all frame slots and the bounded graphics/async-compute plan. Query-pool
  creation failure disables profiling with a diagnostic; it must not make an
  otherwise operational render device fail. `VK_ERROR_DEVICE_LOST` is not an
  allocation-only degradation: notify the owning device, report `DeviceLost`,
  and stop operational promotion. Destroy the pool after the normal device
  idle during shutdown and before `VkDevice` destruction.
- Keep the existing `kMaxTimestampScopes` (256) as the total pass-scope
  ceiling per frame, reserve one begin/end envelope pair per supported actual
  queue, and diagnose exhaustion. Do not resize or replace pools from the
  frame path.
- Determine support per actual queue family from
  `VkQueueFamilyProperties::timestampValidBits > 0`, not only
  `timestampComputeAndGraphics`. Validate that `timestampPeriod` is finite and
  positive. Unsupported actual queues receive no query range and remain
  explicit in the result. A globally ready backend must still return
  per-frame `Unsupported` when every queue used by a nonempty accepted plan
  lacks timestamp bits.
- On slot reuse, preserve this order: existing slot-fence completion proof;
  nonblocking resolution of the prior successfully submitted metadata;
  metadata retirement; then command-recorded reset of the range about to be
  reused. Record `vkCmdResetQueryPool` into the accepted per-queue primary
  submit context outside any render pass and before that queue's first
  timestamp write. Use disjoint slot/queue ranges.
- Use `vkCmdWriteTimestamp2` stages legal for the actual graphics or compute
  queue. Record per-queue envelope begin/end in submit order and per-pass
  begin/end immediately around the actual callback in every accepted serial,
  parallel-secondary, single-queue, and multi-queue lane. If a requested
  async/parallel plan falls back, report the accepted graphics context, not
  the requested lane.
- Resolve with `VK_QUERY_RESULT_64_BIT |
  VK_QUERY_RESULT_WITH_AVAILABILITY_BIT`, without `WAIT` or `PARTIAL`, and
  inspect both values of every pair. Mask each timestamp to its queue family's
  valid-bit width and compute the modular delta before checked
  `ticks * timestampPeriod` conversion. This supports at most one wrap;
  use `steady_clock` from frame planning through completed-slot resolution
  only as a conservative upper-bound guard. When that envelope reaches the
  counter's full period, multiple wraps are ambiguous and resolution fails
  with `Overflow`; never substitute host time for GPU duration.
- Device loss keeps the renderer's prior last-good UI cache marked stale and
  reports device-lost status. It must not publish a fresh telemetry row,
  dereference a dead pool, or invent partial duration evidence. If completed
  slot retirement observes loss inside device `BeginFrame`, return before any
  later reset, acquire, cleanup, or other Vulkan backend call.

## Publication semantics

- Add one plain nested GPU-profile snapshot to
  `Graphics::RenderGraphFrameStats`: current recording/resolution status and
  diagnostic, source, stale flag/sample age in submitted frames, resolved
  submitted-frame number/slot, per-queue envelopes, and pass rows containing
  compiled name/ID, accepted queue, command-record status, availability, and
  native duration.
- A queue envelope means first recorded profiled timestamp to last recorded
  profiled timestamp on that same queue for that resolved frame. Never sum
  pass durations, compare unsynchronized queue clocks, or publish a
  cross-queue total/critical path. A pass that was culled or whose command
  status was skipped must not appear as recorded GPU work.
- Keep a renderer-owned last-good resolved cache for the Frame Graph panel.
  Disabled, unsupported, priming/not-ready, overflow, and device-lost states
  may display that sample only with `Stale=true`, its original frame identity,
  and age. Clear `Core::Telemetry` GPU pass rows whenever there is no fresh
  `NativeGpu` resolution; publish only fresh recorded native pass rows through
  the existing `SetPassGpuTimings`.
- Extend `SandboxEditorRenderGraphModel` and the existing
  `view.frame_graph` presentation to copy/show those fields and the config
  toggle. Do not modify `EditorUiHost` ownership or register another window.

## Slice plan

1. **RHI lifecycle and provenance (`CPUContracted`).** Repair the existing profiler value/error contract, make Null/mock results provenance-honest, and pin the backend-neutral lifecycle with CPU tests. Supported profiler resources are device-lifetime objects. Defer all native-duration claims.
2. **Native Vulkan query lifecycle.** Construct the existing Vulkan adapter, bind query reset/write/resolve to real frame slots and command buffers, handle valid bits/period/capacity/device loss without waits, and pass focused conversion plus Vulkan lifecycle coverage. Defer compiled-pass publication until slot reuse is proven.
3. **Graphics compiled-pass integration.** Add the immutable frame-input bit, instrument actual serial/parallel and graphics/async-compute pass recording, and correlate resolved queue/pass rows into existing telemetry and `RenderGraphFrameStats`. No new profiler service or module may appear.
4. **Config control and existing presentation.** Add the one config field plus synchronous preview/round-trip/apply, sample committed state once into the immutable frame input, and display the renderer snapshot through the existing Frame Graph model/window. This remains a separately mergeable cross-layer control slice.
5. **Operational proof and docs.** Run the non-skipped native smoke for more than `framesInFlight`, prove named-pass timing and truthful unsupported behavior, then land the remaining current-state RHI/Vulkan/renderer/editor documentation. Only this slice reaches the task's final `Operational` maturity.

Each slice is a separately reviewable commit and must pass its focused CPU checks plus all earlier slice gates. Later slices may not weaken or replace earlier provenance/nonblocking contracts.

## Required changes

- [x] Repair the existing RHI contract with explicit monotonic frame identity separate from frame slot, a typed invalid scope result, queue/source provenance, and deterministic invalid-pairing/overflow/unsupported diagnostics.
- [x] Make Null/mock profiling validate lifecycle, names, frame-slot reuse, and not-ready behavior without labeling host-clock durations as native GPU time.
- [x] Add the one default-off config field, route editor and agent/CLI writes through synchronous `EngineConfigControl` preview/apply, and sample committed state once into `RenderFrameInput` after `UiEndCapture`.
- [x] Use the settled app-composed ConfigControl and existing EditorShell Frame Graph contribution; do not add a profiling-specific Engine getter, renderer setter, callback, staging queue, or UI path.
- [x] Construct the device-lifetime Vulkan adapter after successful device initialization and its fixed query pool whenever a promoted actual queue supports native timestamps. Previously submitted metadata is retired only at its later slot-completion proof; adapter lifetime is independent of per-frame recording enablement.
- [x] Bind reset/envelope/pass timestamp commands to the accepted graphics/compute primary and secondary contexts. Reset a reused slot only after its existing fence proof, outside render passes, and keep slot/queue/local/absolute query indexing coherent.
- [x] Respect per-queue-family `timestampValidBits`, finite positive timestamp period, query exhaustion, availability bits, modular wrap, checked conversion, discarded submissions, device loss, and command-buffer lifetime. Resolution never uses `VK_QUERY_RESULT_WAIT_BIT` or adds a profiler-specific frame wait.
- [x] Resolve only completed older frames before slot reuse and retain the last good resolved sample when the newest result is not ready.
- [x] Preplan immutable tokens before worker fan-out and bracket the actual compiled pass callback by stable compiled pass name/ID and accepted queue in serial, parallel-recorded, graphics, fallback, and async-compute paths without locks or duplicate scopes.
- [x] Publish only fresh recorded `NativeGpu` pass rows into existing `Core.Telemetry`; publish detailed current status plus stale last-good sample, per-queue envelopes, provenance, and frame correlation through `RenderGraphFrameStats`.
- [x] Copy the renderer GPU-profile snapshot into the existing Frame Graph model/window, including config-control availability and rejection diagnostics; do not add another presentation contribution.
- [x] Define multi-queue frame timing solely as named per-queue envelopes; do not sum overlapping work or populate an ambiguous global GPU-frame total.

## Tests

- [x] Add `tests/unit/graphics/Test.RHI.Profiler.cpp` to
  `IntrinsicGraphicsRhiCpuUnitTests`. Cover valid-bit widths 0 and 64,
  32-bit wrap (`0xfffffff0 -> 0x20 == 48` ticks), finite/zero/non-finite
  period, checked nanosecond overflow, the host upper bound immediately below
  versus at a full counter period, availability pairs, and slot-local versus
  absolute query bounds.
- [x] Add `tests/contract/graphics/Test.Profiler.cpp` to
  `IntrinsicGraphicsContractCpuTests`. Cover typed-token invalidity,
  frame-number versus slot reuse, begin/end/seal/discard ordering, duplicate
  end, exhaustion, not-ready last-good retention, source/queue provenance,
  and Null lifecycle results with no available native duration.
- [x] Extend
  `tests/contract/graphics/Test.VulkanFailClosedContract.cpp` in
  `IntrinsicGraphicsVulkanContractTests`. Cover the backend-local support
  decision for zero/nonzero queue-family valid bits, invalid timestamp period,
  unsupported actual-frame queues, ordinary query-pool creation failure
  degrading only profiler status, query-pool device loss stopping device
  bootstrap, and the post-retirement device-loss frame-continuation guard.
- [x] Extend
  `tests/contract/graphics/Test.RendererFrameLifecycle.cpp` with exact cases
  for serial, accepted parallel, graphics/async multi-queue, requested-queue
  fallback, discarded submit, and absent profiler. Assert one scope per
  recorded callback, actual-queue attribution, deterministic preplanning,
  per-queue envelopes without summation, stale-cache status, and telemetry
  clearing when no fresh native sample exists. Cover per-frame `Unsupported`
  planning, a successful device frame whose hot-disabled render snapshot must
  not mask current profiler `DeviceLost`, and a failed device `BeginFrame`
  that maps current profiler `DeviceLost`; both loss paths preserve the stale
  last-good frame identity.
- [x] Extend `tests/unit/core/Test.Core.EngineConfigLoad.cpp`,
  `tests/contract/runtime/Test.RuntimeConfigControlFacade.cpp`, and
  `tests/contract/runtime/Test.RuntimeEngineLayering.cpp`, plus the renderer
  lifecycle contract. Prove default false plus JSON round-trip, synchronous
  Editor and AgentCli apply, rejected preview leaves config unchanged, the
  committed value is sampled once in the same frame's render snapshot, hot
  disable records no new scopes, and toggles never create/destroy profiler
  resources.
- [x] Extend `tests/contract/runtime/Test.SandboxEditorModels.cpp` and
  `tests/integration/runtime/Test.SandboxEditorPresentation.cpp`. Prove exact
  status/source/frame/slot/age/queue/pass copying, read-only behavior when
  ConfigControl commands are absent, one preview/apply path, rejection
  diagnostics, and reuse of `view.frame_graph` with no second window.
- [x] Add
  `DefaultRecipeSurfaceGpuSmoke.NativeGpuTimestampsResolveNamedPassesAfterSlotReuse`
  to
  `tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`. Enable
  profiling in config before device boot, run at least
  `2 * framesInFlight + 1` successful frames, and use no profiler-specific
  waits. If the Vulkan device is not operational, use the established
  pre-run capability skip. Once operational, timestamp-capable hardware must
  produce a fresh finite nonzero `NativeGpu` row for a known recorded
  default-recipe pass, with an older submitted-frame number and reused slot.
  An operational device whose actual queue family has zero valid bits must
  instead pass with explicit per-frame `Unsupported` status and no native
  rows, even when the unused queue made the backend globally ready; do not
  skip or substitute CPU timing. Record selected physical-device/API/driver/
  UUID and queue timestamp metadata in GTest XML.
- [x] Extend
  `DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialReadbackWithValidation`
  and
  `DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialAsyncComputeReadbackWithValidation`
  to assert profiling does not change the existing readback/validation
  result, parallel scopes are not duplicated, accepted async work is labeled
  `AsyncCompute`, fallback is labeled `Graphics`, and no cross-queue total is
  exposed. The async test retains its existing capability skip.

## Docs

- [x] Update `src/graphics/rhi/README.md`,
  `src/graphics/vulkan/README.md`, and
  `src/graphics/renderer/README.md` with the repaired API, slot/submission
  lifecycle, per-queue support, reset/write/resolve ordering, availability and
  stale behavior, measured interval, and nonblocking limitations.
- [x] Update `docs/architecture/frame-graph.md`,
  `docs/architecture/engine-config.md`,
  `docs/architecture/runtime-config-control.md`, and `src/runtime/README.md`
  with the synchronous commit plus immutable `RenderFrameInput` sampling lane
  and fresh-only telemetry publication.
- [x] Update `src/app/Sandbox/README.md` with the existing Frame Graph toggle,
  read-only omission behavior, provenance/status display, and lack of a second
  EditorUi contribution.
- [x] If `.cppm` surfaces change, regenerate
  `docs/api/generated/module_inventory.md`. At promotion/retirement, update
  the rendering/global indexes, `REVIEW-003` dependency state, retirement log,
  and `tasks/SESSION-BRIEF.md` through the normal task workflow; those
  lifecycle edits are not part of this backlog-contract amendment.

## Benchmark and claim limits

- [x] Record in the implementation docs that profiling is default-off
  diagnostic instrumentation which adds timestamp/reset/query work and can
  perturb the command stream. This task adds correctness and capability
  evidence only: no benchmark manifest/result, overhead threshold, SLO, or
  performance improvement is claimed.
- [x] Treat raw pass timestamps and per-queue envelopes as diagnostic
  measurements, not wall-clock frame time or comparative performance
  evidence. Any future performance claim requires a stable benchmark ID,
  declared scene/config/warmup/measured-frame count, backend/GPU/driver
  metadata, validated machine-readable result, and comparable baseline under
  the benchmark workflow.

## Acceptance criteria

- [x] Enabling profiling on a supported Vulkan device produces correlated native pass timings from actual command recording after more than one slot-reuse cycle.
- [x] The steady-state render path adds no profiler-specific CPU/GPU wait and
  query resolution never requests blocking or partial results.
- [x] Null/unsupported data cannot be mistaken for native GPU evidence.
- [x] Parallel and multi-queue recording remain race-free, scope each recorded
  callback exactly once, and retain truthful accepted-queue attribution.
- [x] Synchronous config apply is observable at the defined render-input
  snapshot boundary, and hot toggles never change query-pool lifetime.
- [x] The existing Frame Graph panel shows current status and clearly aged
  last-resolved timings without a new service, callback, or window.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicGraphicsRhiCpuUnitTests IntrinsicGraphicsRendererCpuUnitTests IntrinsicGraphicsContractTests IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'CoreEngineConfigLoad|Profiler|Telemetry|RendererFrameLifecycle|RuntimeConfigControlFacade|RuntimeFrameLoopContract|SandboxEditor' --no-tests=error --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanContractTests IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'VulkanFailClosedContract.*Profiler' --no-tests=error --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'NativeGpuTimestamps|ParallelRecordingMatchesSerial' --no-tests=error --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
```

Pre-audit evidence recorded on 2026-07-19 (superseded by the audit-fix
commit; not current-head completion evidence):

- `ci-vulkan` used Clang 23 with ASan+UBSan. The focused `gpu;vulkan`
  selector passed all three non-skipped
  `NativeGpuTimestampsResolveNamedPassesAfterSlotReuse` and
  `ParallelRecordingMatchesSerial*WithValidation` cases (3/3).
- Pre-audit host inspection distinguished loader instance Vulkan 1.4.309 and
  engine-requested Vulkan 1.3, while host GPU0 / an observed NVIDIA GeForce RTX
  3050 physical device reported API 1.4.325, proprietary driver 590.48.01,
  `timestampValidBits = 64`, and `timestampPeriod = 1 ns`. That host-wide
  observation did not prove which physical device the engine selected. The
  post-audit smoke now records selected-device facts and UUID from the
  engine-owned profiler diagnostic; refreshed binding evidence is pending the
  held final rerun.
- The earlier native case completed 8 frames with 3 frames in flight and
  resolved an older `NativeGpu` `SurfacePass` sample into telemetry. Its exact
  duration and frame metadata are intentionally not carried forward as
  evidence for the post-audit head; the held final rerun must replace them.
- Graphics-only and async-compute serial/parallel fixtures retained identical
  readback bytes and stable validation counters. Exact candidate-frame
  resolution, unique scope parity, graphics `SurfacePass`, async-compute
  `PostProcessHistogramPass`, and independent queue envelopes all passed.
- `VulkanFailClosedContract.Profiler*` passed 4/4. Strict layering, test
  layout, documentation-link, task-policy/task-schema, and generated module
  inventory checks passed.

## Forbidden changes

- Blocking query resolution or using `VK_QUERY_RESULT_WAIT_BIT` in the frame path.
- Destroying or recreating profiler/query-pool resources in response to a hot toggle while frames may reference them.
- Reporting host-clock or summed-overlap durations as native GPU frame time.
- Adding another profiler abstraction, telemetry store, config flag, or editor window.
- Reintroducing an Engine domain facade or bypassing the shared validated
  config/UI contribution paths to wire profiling.
- Deferring config mutation to a completed-frame queue; apply commits
  synchronously and only the renderer's immutable per-frame sample is
  boundary-scoped.
- Instrumenting CPU executor callbacks instead of the command-recording scopes they schedule.
- Allocating scope/query IDs under worker-thread locks or reporting a requested
  queue after the accepted execution lane fell back.

## Maturity

- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Achieved `Operational` on the 2026-07-19 NVIDIA/Vulkan host through the
  non-skipped named-pass and slot-reuse smoke above; unsupported hosts retain
  the explicit `CPUContracted`/fail-closed branch.
- Operational evidence requires a non-skipped native-Vulkan smoke with named pass timing and slot reuse; CPU/Null lifecycle tests alone are insufficient.
- Retirement evidence must cite the successful GPU-smoke host, Vulkan backend,
  physical GPU, driver, timestamp-valid-bit capability, frames-in-flight, and
  resolved submitted-frame/slot correlation. An unsupported operational
  device proves only the fail-closed branch, not native `Operational`
  maturity.

## Native API references

- [`vkCmdWriteTimestamp2`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html)
  defines command-buffer and queue-stage requirements.
- [`vkCmdResetQueryPool`](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdResetQueryPool.html)
  requires command recording outside a render pass.
- [`vkGetQueryPoolResults`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)
  defines nonblocking availability-result behavior.
- [Vulkan timestamp-query specification](https://docs.vulkan.org/spec/latest/chapters/queries.html)
  defines queue-family valid bits, timestamp period, and wrap semantics.

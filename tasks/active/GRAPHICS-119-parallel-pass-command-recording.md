---
id: GRAPHICS-119
theme: B
depends_on: []
---
# GRAPHICS-119 — Parallel render-pass command recording via the task scheduler

## Status
- In progress on local `main`; PR not opened.
- Owner/agent: Codex.
- Current slice: Slice C.10 (opt-in Vulkan graphics-queue smoke) completed and
  verified locally.
- Next implementation step: cover remaining Vulkan non-graphics secondary
  execution scope.

## Goal
- Record render-graph pass command buffers in parallel: independent passes
  (by compiled topology) record into per-pass/per-batch secondary command
  contexts on `Core::Tasks` workers, joined deterministically at submit —
  replacing today's strictly serial single-threaded recording.

## Non-goals
- No GPU-side scheduling changes (queue submission order, sync) beyond what
  parallel recording requires; cross-queue timeline synthesis stays as-is.
- No framegraph compilation changes (`GRAPHICS-117` caching lands
  independently; this task consumes whatever compile output exists).
- Blocking the render thread until all recording completes before submit is
  acceptable — this parallelizes recording, it does not pipeline frames.

## Context
- Owner/layer: `graphics/renderer` (executor driving),
  `graphics/rhi` + `graphics/vulkan` (secondary/parallel command context
  contract), `core` consumer (`Core.Tasks` dispatch).
- Today `RenderGraphExecutor::Execute` iterates passes serially on the
  calling thread into one `ICommandContext`
  (`src/graphics/framegraph/Graphics.RenderGraph.Executor.cpp:73-111`,
  `src/graphics/renderer/Graphics.Renderer.cpp:2789-2809`), and even the
  multi-queue `executeSubmitPlan` records batches sequentially (`:2847+`).
  The compiled graph already carries `TopologicalLayerByPass`, which bounds
  a straightforward layer-parallel decomposition; finer batching by actual
  dependency chains is allowed if it stays deterministic.
- Pass bodies must be audited for shared mutable renderer state (descriptor
  pools, upload allocators, stats) — thread-safe or per-context instances
  are part of this task's scope.
- `CORE-005` (non-blocking graph submit) is complementary but not required:
  a fork/join over `Scheduler::Dispatch` + `CounterEvent` suffices.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R13.

## Backends
- Backend axis: Vulkan secondary command buffers (or parallel primaries per
  batch) behind an RHI contract; Null device provides a recording-order
  bookkeeping implementation so determinism is CPU-provable.

## Slice plan
- **Slice A (this slice).** Add a backend-neutral `RenderGraphExecutor`
  layer-parallel record/join path that uses `TopologicalLayerByPass` to record
  independent pass work through `Core::Tasks` workers, then emits barriers and
  submit callbacks in the existing serial topological order. Pin deterministic
  submit order, worker distribution, and fail-closed error propagation with
  CPU/null graphics contract tests. Defers RHI command-context allocation,
  renderer route integration, Vulkan secondary command buffers, GPU smoke, and
  benchmark evidence to later slices.
- **Slice B.** Add the RHI/null parallel command-context acquisition contract
  and wire renderer serial fallback/debug selection without changing Vulkan
  default behavior.
- **Slice C.** Add Vulkan secondary/parallel command-context implementation and
  route framegraph pass recording through it behind the fallback flag.
- **Slice D.** Record benchmark evidence and cite an actually-run opt-in
  `gpu;vulkan` smoke before retiring the task.

## Required changes
- [x] Slice A: executor layer-parallel record/join API with deterministic
      serial submit callbacks and `Core::Tasks` worker dispatch when available.
- [x] Slice B: RHI parallel command-context plan/acquire/submit seam with
      default unsupported behavior and Null CPU bookkeeping contexts.
- [x] Slice B: renderer debug selector keeps serial fallback selectable and
      records accepted/fallback stats without changing Vulkan default behavior.
- [x] Slice C.1: Vulkan accepts graphics-queue parallel command-context plans
      with backend-local secondary command buffers and executes them from the
      primary context in compiled submit order.
- [x] Slice C.2: renderer exposes worker fan-out stats and pins the accepted
      parallel-context path to caller-thread recording until shared pass state
      is isolated or synchronized.
- [x] Slice C.3: command-record diagnostics accumulate through a guarded
      frame-local renderer accumulator and publish after graph record/join.
- [x] Slice C.4: picking and histogram readback issue counters and per-slot
      metadata route through guarded renderer helpers from pass callbacks.
- [x] Slice C.5a: transient-debug, visualization-overlay, and ImGui dynamic
      upload helpers serialize per-frame reset plus pass-body upload/execute
      sections behind a shared renderer guard while parallel contexts remain on
      the caller thread.
- [x] Slice C.5b: postprocess pass helpers serialize per-frame bloom scratch,
      histogram viewport/buffer, and AA stage pass-object recording behind a
      shared renderer guard while parallel contexts remain on the caller
      thread.
- [x] Slice C.5c: Vulkan accepted parallel command contexts allocate and own
      frame-scoped command pools for their secondary command buffers so future
      worker recording does not borrow externally synchronized frame pools.
- [x] Slice C.6: renderer enables scheduler worker fan-out for accepted
      single-queue parallel context plans when `Core::Tasks::Scheduler` is
      initialized; frame-sampled descriptor bridge updates and mock-device
      request bookkeeping are guarded for worker recording.
- [x] Slice C.8: renderer uses accepted parallel command-context plans for
      CPU/null multi-queue submit frames, records pass bodies through scheduler
      workers when available, and joins submitted contexts back through each
      queue-submit batch without changing queue order, timeline waits/signals,
      or barrier placement.
- [x] Slice C.9: add PR-fast benchmark smoke coverage for serial executor
      recording vs scheduler-backed parallel record/join on a deterministic
      pass-heavy CPU/null graph, emitting machine-readable baseline/probe
      metrics with no performance-win adoption claim.
- [x] Slice C.10: add opt-in promoted Vulkan smoke coverage that compares
      serial and accepted graphics-queue parallel readback bytes under
      validation, with postprocess disabled so the current Vulkan secondary
      command implementation is not forced through the deferred non-graphics
      plan.
- [x] RHI contract for parallel recording: acquire per-thread/per-batch
      command contexts, record independently, submit in compiled order;
      Null + Vulkan implementations.
- [x] Audit and fix thread-affinity of pass-recording state (descriptor
      allocation, dynamic uploads, shared pass helper state)
      — per-context or
      synchronized, chosen per site and documented.
- [x] Parallel executor path: fan pass recording out by topological
      layer/batch via `Core::Tasks::Scheduler`, join on a `CounterEvent`,
      then emit barrier packets and submit in the compiled serial order so
      GPU-visible ordering is unchanged.
- [x] Keep the serial path selectable (config/debug flag) as the fallback
      and determinism reference.

## Tests
- [x] Slice A CPU/null contract: layer-parallel recording emits the same
      barrier/pass submit order as serial execution.
- [x] Slice A CPU/null contract: worker recording distributes independent
      passes across `Core::Tasks` workers and reports deterministic stats.
- [x] Slice A CPU/null contract: failed parallel record callbacks join all
      scheduled work and fail closed before serial submit.
- [x] Slice B CPU/null contract: Null and Mock devices expose deterministic
      per-pass command-context plan/acquire/submit bookkeeping.
- [x] Slice B renderer contract: enabled parallel selector falls back to serial
      when the device declines and uses accepted per-pass contexts in compiled
      submit order when the mock device accepts.
- [x] Slice C.1 Vulkan fail-closed contract: non-operational Vulkan declines
      parallel context plans and routes acquisition back to the graphics
      context.
- [x] Slice C.2 renderer contract: accepted parallel context plans report no
      scheduler worker tasks and record every pass on the caller thread.
- [x] Slice C.3 renderer contract: accepted parallel context plans still
      publish internally consistent command-record diagnostics from the
      isolated accumulator.
- [x] Slice C.4 renderer contract: picking and histogram readback copy
      counters and drain metadata still record and drain through the existing
      CPU/null lifecycle tests after routing through guarded helpers.
- [x] Slice C.5a renderer contract: accepted parallel context plans record the
      transient-debug, visualization-overlay, and ImGui dynamic upload routes
      with the existing per-helper diagnostics intact.
- [x] Slice C.5b renderer contract: accepted parallel context plans still
      record the postprocess umbrella and histogram routes after guarding the
      shared postprocess pass helper state.
- [x] Slice C.5c Vulkan fail-closed/build contract: non-operational Vulkan
      still declines parallel contexts, and the Vulkan backend builds with
      per-context command-pool ownership for accepted graphics-queue plans.
- [x] Slice C.6 renderer contract: accepted parallel context plans dispatch
      pass recording through scheduler worker tasks when the scheduler is
      initialized and keep compiled serial submit order.
- [x] Slice C.7 CPU/null contract: seeded DAG graph shapes preserve the exact
      serial barrier/pass event stream under scheduler-backed parallel
      record/join.
- [x] Slice C.8 renderer contract: accepted async-compute queue-submit plans
      request and submit non-graphics parallel command contexts, dispatch pass
      recording through scheduler workers, and preserve the existing
      queue-submit context usage.
- [x] Slice C.9 benchmark contract: manifest validates, runner emits result
      JSON with serial/parallel runtime diagnostics plus checksum parity, and
      result JSON validates under the benchmark schema.
- [x] CPU/null contract: parallel recording produces the same
      pass-execution/barrier submission order as serial (bookkeeping
      comparison over randomized graphs).
- [x] CPU/null contract: recording work actually distributes across workers
      (probe), with the join deterministic.
- [x] Opt-in `gpu;vulkan` smoke: default recipe graphics-queue plan
      image-identical serial vs parallel; validation layers clean under
      parallel recording. The smoke disables postprocess to keep the plan
      graphics-only; Vulkan non-graphics secondary execution remains later
      backend scope.
- [x] PR-fast benchmark: recording CPU ms/frame serial vs parallel on a
      pass-heavy synthetic recipe.

## Docs
- [x] Slice A: document executor layer-parallel record/join semantics and
      callback thread-safety expectations.
- [x] Slice B: document RHI/null command-context acquisition, renderer fallback
      selector, and deferred Vulkan worker fan-out scope.
- [x] Slice C.1: document Vulkan secondary command-buffer acquisition,
      graphics-queue primary-context execution, frame-slot lifetime, and
      deferred non-graphics queue / worker fan-out scope.
- [x] Slice C.2: document the worker fan-out audit finding and the mutable
      renderer surfaces that keep `Core::Tasks` dispatch disabled.
- [x] Slice C.3: document command-record diagnostics isolation and the
      remaining mutable surfaces that still block worker fan-out.
- [x] Slice C.4: document readback issue-state isolation and the remaining
      upload/helper/command-pool blockers.
- [x] Slice C.5a: document dynamic upload helper serialization and the
      remaining shared pass helper state / Vulkan command-pool blockers.
- [x] Slice C.5b: document shared postprocess pass helper serialization and
      the remaining Vulkan command-pool blocker.
- [x] Slice C.5c: document per-context Vulkan command-pool ownership and the
      remaining worker fan-out, benchmark, and opt-in Vulkan smoke scope.
- [x] Slice C.6: document scheduler-backed renderer worker fan-out, the guarded
      frame-sampled descriptor bridge, and the remaining non-graphics queue /
      benchmark / opt-in Vulkan smoke scope.
- [x] Slice C.7: task record documents seeded CPU/null determinism coverage;
      no architecture docs changed.
- [x] Slice C.8: document accepted CPU/null multi-queue fan-out and the
      remaining Vulkan non-graphics secondary-execution scope.
- [x] Slice C.9: document the render-graph parallel-recording smoke benchmark
      and record that it is benchmark evidence, not a renderer-wide performance
      win claim.
- [x] Slice C.10: document the opt-in
      `DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialReadbackWithValidation`
      Vulkan smoke and the remaining non-graphics secondary-execution scope.
- [x] Update `docs/architecture/frame-graph.md` and
      `src/graphics/renderer/README.md` (threading model, fallback flag).

## Acceptance criteria
- [x] Deterministic submission order proven by contract tests; identical
      Vulkan frame output cited from an actually-run smoke.
- [x] Benchmark evidence recorded (or an honest "no win at current pass
      counts, kept behind flag" conclusion).
- [x] Serial fallback intact; CPU gate green; layering gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
ctest --test-dir build/ci --output-on-failure -L 'gpu' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphParallelRecording|GraphicsOwnershipTransferBarriers|GraphicsRenderGraph.Execute' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphParallelRecording|RenderGraphValidation|CrossQueueTimeline|FrameRecipeContract|RendererFrameLifecycle|OwnershipTransferBarriers|QueueAffinity' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice B focused verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'GraphicsQueueAffinity|RendererFrameLifecycle\..*ParallelRecording|RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'GraphicsQueueAffinity|RendererFrameLifecycle|RenderGraphParallelRecording|RenderGraphValidation|CrossQueueTimeline|FrameRecipeContract|OwnershipTransferBarriers|QueueAffinity' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice C.1 verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target ExtrinsicBackendsVulkan
cmake --build --preset ci --target IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -R 'VulkanFailClosedContract\..*ParallelCommand|VulkanFailClosedContract|VulkanOperational' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicGraphicsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -R 'VulkanFailClosedContract\..*ParallelCommand|VulkanFailClosedContract|RendererFrameLifecycle\..*ParallelRecording|GraphicsQueueAffinity\..*ParallelCommand|RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice C.2 verification run locally on 2026-07-06:

```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\..*ParallelRecording|GraphicsQueueAffinity\..*ParallelCommand|RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice C.3 verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\..*ParallelRecording|GraphicsQueueAffinity\..*ParallelCommand|RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
git diff --check
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice C.4 verification run locally on 2026-07-06:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\.(PickingReadback|HistogramReadback|ParallelRecording)|RenderGraphParallelRecording|GraphicsQueueAffinity\..*ParallelCommand' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
git diff --check
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice C.5a verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\.(ParallelRecording|.*DynamicUpload)|TransientDebugSurfacePassContract|VisualizationOverlayPassContract|ImGuiPassContract|RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
git diff --check
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation|TransientDebugSurfacePassContract|VisualizationOverlayPassContract|ImGuiPassContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
```

Clean-workshop manual scorecard for Slice C.5a: row 3 `n/a` (no public
`.cppm` API surface changed), row 4 `pass` (one renderer guard member is owned
by the GRAPHICS-119 dynamic-upload synchronization seam and is not a new
subsystem), row 5 `n/a` (no new frame-graph pass), row 6 `n/a` (no recipe edge
changes). Findings: none; no follow-up task ID required.

Slice C.5b verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\.(ParallelRecording|PostProcess)|PostProcessChainContract|RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
git diff --check
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation|PostProcessChainContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
```

Clean-workshop manual scorecard for Slice C.5b: row 3 `n/a` (no public
`.cppm` API surface changed), row 4 `pass` (one renderer guard member is owned
by the GRAPHICS-119 postprocess pass-state synchronization seam and is not a
new subsystem), row 5 `n/a` (no new frame-graph pass), row 6 `n/a` (no recipe
edge changes). Findings: none; no follow-up task ID required.

Slice C.5c verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target ExtrinsicBackendsVulkan
cmake --build --preset ci --target IntrinsicGraphicsVulkanContractTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'VulkanFailClosedContract\..*ParallelCommand|VulkanFailClosedContract|GraphicsQueueAffinity\..*ParallelCommand|RenderGraphParallelRecording|RendererFrameLifecycle\..*ParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'VulkanFailClosedContract|RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
```

Clean-workshop manual scorecard for Slice C.5c: row 3 `n/a` (backend-internal
`.cppm` storage only; no public RHI/renderer API shape changed), row 4 `pass`
(per-context Vulkan command-pool ownership is owned by the GRAPHICS-119 backend
parallel-command-context seam and is not a new subsystem), row 5 `n/a` (no new
frame-graph pass), row 6 `n/a` (no recipe edge changes). Findings: none; no
follow-up task ID required.

Slice C.6 verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\.ParallelRecordingUsesSchedulerWorkersWhenAvailable' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\.(ParallelRecording|.*DynamicUpload)|RenderGraphParallelRecording|GraphicsQueueAffinity\..*ParallelCommand|TransientDebugSurfacePassContract|VisualizationOverlayPassContract|ImGuiPassContract|PostProcessChainContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation|PostProcessChainContract|TransientDebugSurfacePassContract|VisualizationOverlayPassContract|ImGuiPassContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
```

Clean-workshop manual scorecard for Slice C.6: row 3 `n/a` (no public `.cppm`
API surface changed), row 4 `pass` (the renderer descriptor guard and mock
request guard are owned by the GRAPHICS-119 worker-recording seam and are not a
new subsystem), row 5 `n/a` (no new frame-graph pass), row 6 `n/a` (no recipe
edge changes). Findings: none; no follow-up task ID required.

Slice C.7 verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphParallelRecording\.SeededDagsPreserveSerialSubmitOrder' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphParallelRecording' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

Slice C.8 verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle\.(ParallelRecordingUsesAcceptedContextsForAsyncComputeQueuePlan|ParallelRecordingUsesSchedulerWorkersWhenAvailable|ParallelRecordingUsesAcceptedContextPlanInSerialSubmitOrder|AsyncComputeQueuePlanIncrementsUtilizationStat)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|RenderGraphParallelRecording|GraphicsQueueAffinity|RenderGraphValidation|CrossQueueTimeline' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice C.8 full CPU-supported gate result: 3596/3596 tests passed.

Clean-workshop manual scorecard for Slice C.8: row 3 `n/a` (no public
`.cppm` API surface changed), row 4 `pass` (the queue-submit parallel join is
owned by the existing GRAPHICS-119 renderer/RHI command-context seam and is not
a new subsystem), row 5 `n/a` (no new frame-graph pass), row 6 `n/a` (no recipe
edge changes). Findings: none; no follow-up task ID required.

Slice C.9 verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
build/ci/bin/IntrinsicBenchmarkSmoke /tmp/graphics119-c9-benchmark
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/graphics119-c9-benchmark --strict
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke\.(Run|Validate)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

Slice C.9 benchmark result:

```text
benchmark_id: rendering.rendergraph_parallel_recording.smoke
backend: cpu_reference
dataset: builtin.synthetic_parallel_recording.256_independent_passes
serial_record_ms: 0.310376
parallel_record_ms: 0.438259
parallel_to_serial_runtime_ratio: 1.412027
quality_error_l2: 0.0
parallel_worker_task_count: 256
parallel_caller_record_count: 0
adoption_claim: false
```

Conclusion: checksum parity holds and scheduler fan-out is exercised, but this
PR-fast CPU smoke does not show a win at the current synthetic pass count/work
shape. The parallel renderer path remains behind the debug selector until the
remaining non-graphics Vulkan secondary-execution evidence exists.

Slice C.10 focused verification run locally on 2026-07-07:

```bash
cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke\.ParallelRecordingMatchesSerialReadbackWithValidation' -L 'gpu' -L 'vulkan' --timeout 120
```

Slice C.10 opt-in Vulkan smoke result: 1/1 tests passed. The smoke requested
validation, disabled the postprocess extension to keep the current Vulkan plan
graphics-only, compared serial and parallel default-recipe debug-view readback
bytes, asserted `ParallelRecordingAccepted == true`, and observed no
fallback/validation counter increment across the parallel frame.

Slice C.10 final verification run locally on 2026-07-07:

```bash
python3 tools/agents/generate_session_brief.py
git diff --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Slice C.10 full CPU-supported gate result: 3596/3596 tests passed.

## Forbidden changes
- Nondeterministic submission order or frame output.
- Passing `Vk*` types through RHI/renderer/framegraph public APIs.
- Making the parallel path the only path before non-graphics Vulkan secondary
  execution is covered or explicitly deferred.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  determinism/distribution contracts on Null. The graphics-queue Vulkan
  secondary path has opt-in `gpu;vulkan` smoke evidence; non-graphics Vulkan
  secondary execution remains later backend scope.

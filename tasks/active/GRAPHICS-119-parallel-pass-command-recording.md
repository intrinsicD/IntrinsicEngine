---
id: GRAPHICS-119
theme: B
depends_on: []
---
# GRAPHICS-119 — Parallel render-pass command recording via the task scheduler

## Status
- In progress on local `main`; PR not opened.
- Owner/agent: Codex.
- Current slice: Slice C.3 (command-record diagnostics isolation) completed and
  verified locally.
- Next implementation step: Slice C.4 — isolate or synchronize dynamic upload
  helpers, readback counters, shared pass helper state, and Vulkan command-pool
  ownership before enabling worker fan-out behind the fallback flag.

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
- [ ] RHI contract for parallel recording: acquire per-thread/per-batch
      command contexts, record independently, submit in compiled order;
      Null + Vulkan implementations.
- [ ] Audit and fix thread-affinity of pass-recording state (descriptor
      allocation, dynamic uploads, readback counters, shared pass helper state)
      — per-context or
      synchronized, chosen per site and documented.
- [ ] Parallel executor path: fan pass recording out by topological
      layer/batch via `Core::Tasks::Scheduler`, join on a `CounterEvent`,
      then emit barrier packets and submit in the compiled serial order so
      GPU-visible ordering is unchanged.
- [ ] Keep the serial path selectable (config/debug flag) as the fallback
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
- [ ] CPU/null contract: parallel recording produces the same
      pass-execution/barrier submission order as serial (bookkeeping
      comparison over randomized graphs).
- [ ] CPU/null contract: recording work actually distributes across workers
      (probe), with the join deterministic.
- [ ] Opt-in `gpu;vulkan` smoke: default sandbox recipe image-identical
      serial vs parallel; validation layers clean under parallel recording.
- [ ] PR-fast benchmark: recording CPU ms/frame serial vs parallel on a
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
- [x] Update `docs/architecture/frame-graph.md` and
      `src/graphics/renderer/README.md` (threading model, fallback flag).

## Acceptance criteria
- [ ] Deterministic submission order proven by contract tests; identical
      Vulkan frame output cited from an actually-run smoke.
- [ ] Benchmark evidence recorded (or an honest "no win at current pass
      counts, kept behind flag" conclusion).
- [ ] Serial fallback intact; CPU gate green; layering gate green.

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

## Forbidden changes
- Nondeterministic submission order or frame output.
- Passing `Vk*` types through RHI/renderer/framegraph public APIs.
- Making the parallel path the only path before the Vulkan smoke is cited.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  determinism/distribution contracts on Null. The closing slice owns the
  `gpu;vulkan` smoke citation.

---
id: GRAPHICS-119
theme: B
depends_on: []
---
# GRAPHICS-119 — Parallel render-pass command recording via the task scheduler

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

## Required changes
- [ ] RHI contract for parallel recording: acquire per-thread/per-batch
      command contexts, record independently, submit in compiled order;
      Null + Vulkan implementations.
- [ ] Audit and fix thread-affinity of pass-recording state (descriptor
      allocation, dynamic uploads, per-pass stats) — per-context or
      synchronized, chosen per site and documented.
- [ ] Parallel executor path: fan pass recording out by topological
      layer/batch via `Core::Tasks::Scheduler`, join on a `CounterEvent`,
      then emit barrier packets and submit in the compiled serial order so
      GPU-visible ordering is unchanged.
- [ ] Keep the serial path selectable (config/debug flag) as the fallback
      and determinism reference.

## Tests
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
- [ ] Update `docs/architecture/frame-graph.md` and
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

## Forbidden changes
- Nondeterministic submission order or frame output.
- Passing `Vk*` types through RHI/renderer/framegraph public APIs.
- Making the parallel path the only path before the Vulkan smoke is cited.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` for the
  determinism/distribution contracts on Null. The closing slice owns the
  `gpu;vulkan` smoke citation.

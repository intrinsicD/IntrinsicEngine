---
id: CORE-008
theme: F
depends_on: []
---
# CORE-008 — Compiled task-graph plan reuse across executions

## Goal
- Stop recompiling structurally-unchanged CPU task graphs every execution:
  `Core.Dag.TaskGraph` keeps its compiled topology across `Reset()` when the
  registered pass set is unchanged, and the two per-frame drivers (the
  fixed-step ECS `FrameGraph` in `Engine`, `Graphics.RenderPrepPipeline`)
  adopt the reuse path.

## Non-goals
- Render-graph (framegraph) compile caching — owned by `GRAPHICS-117`.
- New execution APIs (`CORE-005`) or scheduler changes (`CORE-007`).
- Caching across *changed* pass sets (a structural change recompiles; no
  partial/incremental recompile in this task).

## Context
- Owner/layer: `core` for the cache mechanism; `runtime` and
  `graphics/renderer` for adoption.
- Today every fixed sim tick re-registers the same three ECS system passes
  and runs `Compile → Execute → Reset`
  (`src/runtime/Runtime.Engine.cpp:546-584`,
  `Runtime.EcsSystemBundle.cpp:15-34`), and `RenderPrepPipeline` constructs a
  brand-new 9-pass `TaskGraph` per `Run()`
  (`src/graphics/renderer/Graphics.RenderPrepPipeline.cpp:234`). Each
  `Compile()` allocates edge sets/maps with a `std::string DomainReason`
  per edge even when empty, per-pass name strings, successor/predecessor
  vector-of-vectors, and a priority queue
  (`src/core/Core.Dag.TaskGraph.cpp:221-227, 742`). The graph already has a
  `CanUsePlan()` notion, but `Reset()` discards everything each tick.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R16 (CPU half), scheduler finding 7.
- ARCH-013 re-review (2026-07-08): Decision unchanged. Plan reuse remains a
  core DAG optimization; the runtime adoption point must respect the retired
  `RunFrame()` seam and avoid adding new command drains, event pumps, or
  module-owned scheduling paths.

## Required changes
- [ ] Split "clear execution state" from "clear structure": a reset flavor
      that keeps passes + compiled topology and only rewinds per-execution
      state (remaining-deps, timings), invalidated automatically by any
      structural mutation (AddPass/DependsOn/resource decl).
- [ ] Key reuse on a structure fingerprint (pass identities, declared
      accesses, edges, options that affect compilation) so an unchanged
      registration replay is detected cheaply, or provide an explicit
      retained-registration mode where callers declare passes once and only
      re-bind per-tick closures — pick one, document the choice.
- [ ] Store edge diagnostic strings lazily (only materialized on compile
      failure/dump), intern pass names.
- [ ] Adopt in `Engine::RunFixedStepSimulationTicks`: unchanged
      pass set (the common `OnSimTick`-adds-nothing case) executes with zero
      recompiles; app-added passes trigger recompile transparently.
- [ ] Adopt in `Graphics.RenderPrepPipeline`: reuse the pipeline graph
      across `Run()` calls.

## Tests
- [ ] Contract: unchanged replay executes without recompiling (compile-count
      counter exposed via stats) and produces identical execution order
      constraints.
- [ ] Contract: adding/removing a pass or changing a declared access after
      reuse invalidates the plan and recompiles.
- [ ] Existing frame-graph, ECS-system, and render-prep suites stay green.
- [ ] PR-fast micro-benchmark: per-tick compile overhead before/after for
      the 3-pass ECS bundle and the 9-pass render-prep graph.

## Docs
- [ ] Update `docs/architecture/task-graphs.md` (plan reuse contract,
      invalidation rules).

## Acceptance criteria
- [ ] Steady-state sandbox frame performs zero task-graph compiles in the
      fixed-step path (proven by the counter in a contract test).
- [ ] Benchmark evidence recorded for the compile-overhead reduction.
- [ ] Default CPU gate green; no layering changes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Stale-plan execution after structural mutation (invalidation must be
  fail-closed, never best-effort).
- Perf claims without the benchmark numbers.
- Touching the render graph (framegraph) — that is `GRAPHICS-117`.

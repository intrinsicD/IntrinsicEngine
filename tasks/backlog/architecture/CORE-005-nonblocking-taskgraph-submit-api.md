---
id: CORE-005
theme: F
depends_on:
  - BUG-055
---
# CORE-005 — Non-blocking TaskGraph submission and completion API

## Goal
- Give `Extrinsic.Core.Dag.TaskGraph` a non-blocking execution mode: a
  `Submit()`-style entry point that returns a completion token (the
  graph-local `CounterEvent` already exists), so callers can start a graph,
  do other work (including across frames), and later poll or wait — and make
  the existing blocking wait park or help-run instead of yield-spinning.

## Non-goals
- No scheduler-level priority lanes or queue redesign — owned by `CORE-007`.
- No compiled-plan caching — owned by `CORE-008`.
- No removal of GPU/streaming domain vocabulary — owned by `CORE-006`.
- No conversion of `Runtime.DerivedJobGraph`/`StreamingExecutor` onto
  TaskGraph in this task; this ships the core capability they would need.

## Context
- Owner/layer: `core` (`Core.Dag.TaskGraph`, `Core.Tasks`).
- Today `Execute()` blocks the caller until the whole graph finishes and the
  caller busy-polls: it drains main-thread-only passes, else
  `std::this_thread::yield()` (`src/core/Core.Dag.TaskGraph.cpp:999-1020`) —
  burning a core without helping the scheduler (never steals/pops) and
  without parking. There is no submit/poll/join API; `Reset()` asserts while
  `Executing`. Non-CPU `QueueDomain` graphs cannot execute at all, which is
  why streaming work bypasses the DAG (`Runtime.DerivedJobGraph` reimplements
  dependency handling over `StreamingExecutor`).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R12.
- Gated on `BUG-055` because completion-state lifetime must be safe before it
  can escape `Execute`'s stack frame as a token.

## Required changes
- [ ] Add a non-blocking submission API returning a completion handle
      (poll `IsReady()`, blocking `Wait()`); heap/shared-own the execution
      state so it legally outlives the submitting scope (builds on the
      BUG-055 restructure).
- [ ] Keep main-thread-only passes correct under submission: define and
      document how the owning thread drains the main-thread queue for a
      submitted graph (explicit `PumpMainThreadPasses()` on the handle is
      acceptable).
- [ ] Rework the blocking wait: while the graph runs, the calling thread
      help-executes scheduler work (inject queue and stealing) and parks on
      the completion `CounterEvent` when idle, instead of `yield()` spinning.
- [ ] Make `Execute()` a thin wrapper over submit+wait so there is one code
      path.
- [ ] Preserve `Reset()`-while-executing protection with the new lifetime
      model (fail-closed error, not assert-only).

## Tests
- [ ] Contract: submitted graph completes with correct dependency order while
      the submitting thread runs unrelated work before waiting.
- [ ] Contract: main-thread-only passes execute on the owning thread via the
      documented pump path for a submitted graph.
- [ ] Contract: blocking wait makes progress when all workers are saturated
      (help-run proven with a single-worker-forced configuration).
- [ ] Contract: `Reset()` during a live submission fails closed.
- [ ] Existing `CoreTaskGraph.*` and `CoreTasks.*` suites stay green.

## Docs
- [ ] Update `docs/architecture/task-graphs.md` with the submit/completion
      model and the main-thread pump contract.
- [ ] Update `src/core/README.md` module notes.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if `.cppm` surfaces
      change.

## Acceptance criteria
- [ ] A graph can be submitted, progressed, and completed without the caller
      blocking; completion observable via token.
- [ ] The blocking path no longer spins: CPU profile of an idle wait shows
      parked caller, and the caller demonstrably executes stolen tasks under
      saturation.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Introducing domain-specific (graphics/streaming) knowledge into core.
- Silent behavior change of existing `Execute()` callers.
- Timing-based synchronization.

## Maturity
- Target: `CPUContracted`; the API is fully provable on the CPU gate.
  No `Operational` follow-up is owed (consumers adopt it in their own tasks,
  e.g. `GRAPHICS-119`, future streaming-graph unification).

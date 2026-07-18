---
id: CORE-006
theme: F
depends_on: []
---
# CORE-006 — Domain-free core task/DAG vocabulary

## Goal
- Remove assets/geometry/physics/graphics vocabulary from the core task
  system: `TaskKind` becomes an opaque caller-defined token, and
  `Core.Dag.TaskGraph` stops carrying GPU/streaming domain semantics
  (queue budgets, render-graph documentation language, lane placeholders)
  that belong to higher-layer adapters.

## Non-goals
- No behavioral scheduling changes (priorities, queues) — `CORE-007`.
- No new execution APIs — `CORE-005`.
- No renaming of module files (mechanical moves stay out of this task).

## Context
- Owner/layer: `core` (`Core.Dag.Scheduler.Types`, `Core.Dag.TaskGraph`),
  with mechanical follow-through in the only real consumers
  (`src/runtime/Runtime.DerivedJobGraph.cppm:92-93` imports the enums;
  `Core.FrameGraph`, `Graphics.RenderPrepPipeline` use the graph).
- Today core enumerates domain concepts: `TaskKind` lists
  `AssetIO, AssetDecode, AssetUpload, GeometryProcess, PhysicsStep,
  RenderPass` (`src/core/Core.Dag.Scheduler.Types.cppm:32-41`);
  `BuildConfig` carries `queueBudgetGpu/queueBudgetStreaming`;
  `EdgeReason::DomainSpecific` is documented as "reserved for GPU
  render-graph callers"; `QueueDomain` changes API behavior (`Execute()`
  errors on non-CPU, `src/core/Core.Dag.TaskGraph.cpp:879-884`); and
  `ResolveLane` is a dead placeholder that ignores its `order` parameter
  (`Core.Dag.TaskGraph.cpp:816-833`). No layering violation exists (the
  dependency table holds), but core is not generic: lower-layer vocabulary
  should not enumerate upper-layer domains.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R6.
- ARCH-013 re-review (2026-07-08): Decision unchanged as a discovered
  adjacent row. ADR-0024 strengthens the need for this task: core remains
  domain-free, while runtime/module adapters own any taxonomy consumed by
  `JobService`, render prep, or streaming work.

## Status

- Status: `in-progress`.
- Owner: Codex.
- Branch: `codex/core-006-domain-free-v2`.
- Next verification: adapt the preserved task-specific implementation to
  current `main`, then run the focused core/runtime graph contracts.

## Required changes
- [ ] Replace the `TaskKind` domain enum with an opaque token
      (e.g. `std::uint8_t`-backed strong type); define the domain taxonomy
      where it is consumed (`runtime`), preserving current values so
      diagnostics stay stable.
- [ ] Replace `QueueDomain`-driven API branching with an explicit execution
      mode (execute-inline vs plan-only); move Gpu/Streaming domain naming
      and `queueBudget*` plumbing out of core `BuildConfig` into the callers
      that need plans, or delete if unused.
- [ ] Delete `ResolveLane` or move lane assignment to the domain-specific
      `BuildPlan` caller; remove the "reserved for GPU render-graph callers"
      contract language from core doc comments.
- [ ] Mechanically update consumers (`Runtime.DerivedJobGraph`,
      `Core.FrameGraph`, `Graphics.RenderPrepPipeline`, tests) with no
      behavior change.

## Tests
- [ ] Existing `CoreTaskGraph.*`, frame-graph, and render-prep suites stay
      green with unchanged semantics.
- [ ] Contract: plan-only mode still emits the same deterministic plan for a
      fixed graph (golden comparison against pre-change output).

## Docs
- [ ] Update `docs/architecture/task-graphs.md` (core is domain-free;
      runtime owns the taxonomy).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if `.cppm`
      surfaces change.

## Acceptance criteria
- [ ] `src/core/` contains no asset/geometry/physics/render/streaming task
      vocabulary (grep-clean for the removed identifiers).
- [ ] All consumers compile against the opaque-token API with identical
      runtime behavior; default CPU gate green.
- [ ] `python3 tools/repo/check_layering.py --root src --strict` passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Any scheduling behavior change (this is a vocabulary/ownership refactor).
- Mixing in the CORE-005/CORE-007/CORE-008 work.
- Breaking stored diagnostics/telemetry that name current task kinds without
  a compatibility note.

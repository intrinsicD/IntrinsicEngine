---
id: BUG-072
theme: G
depends_on:
  - BUG-069
---
# BUG-072 — Declarative sim-system signal fields create no per-tick FrameGraph edge

## Status
- Completed 2026-07-10 at `CPUContracted` on branch `main` in this workspace.
- Implementation: `f45371c6` derives per-tick edges from the declarative fields;
  `c3794716` and the completed `BUG-069` baseline-bundle work provide the
  external `TransformUpdate` contract.
- Commit: implementation commits above; the regression and retirement closure
  are this local task commit.

## Goal
- Make `SimSystemDesc::WaitForSignals`/`SignalLabels` produce real per-tick
  FrameGraph ordering edges, so module ordering no longer depends on boot-time
  insertion order surviving into execution. This is the durable fix that
  `BUG-069` Slice B defers to.

## Non-goals
- No change to the core `FrameGraph` hazard model.
- No removal of the boot-time canonical sort in `FinalizeForBoot`.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.ModuleSchedule.*`,
  `src/runtime/Runtime.Module.cppm`.
- At discovery, `RegisterSimSystemsForTick` applied only `record.Desc.Setup` to
  the builder; it never translated the declarative
  `WaitForSignals`/`SignalLabels` fields into `builder.WaitFor`/
  `builder.Signal` calls.
- Consequently, the declarative fields were load-bearing **only** for
  `FinalizeForBoot`'s boot-time insertion order. Per-tick ordering among module
  systems rested entirely on insertion order surviving into execution, which
  held only because module descs default to `MainThreadOnly=true,
  AllowParallel=false` (`Runtime.Module.cppm:72-76`). It breaks if a module sets
  `AllowParallel=true`.
- The contract tests masked this by redundantly hand-calling `b.Signal`/
  `b.WaitFor` inside their `Setup` lambdas, so the declarative fields were never
  exercised in isolation and the same edge had to be declared twice.
- `f45371c6` now translates the fields into each tick's `FrameGraph`; the
  baseline bundle exposes `TransformUpdate` through the same signal contract.

## Required changes
- [x] In `RegisterSimSystemsForTick`, translate each system's `WaitForSignals`
      into `builder.WaitFor(...)` and each `SignalLabels` entry into
      `builder.Signal(...)` around the system's `Setup`, so the per-tick graph
      carries the declared edges.
- [x] Route the promoted baseline bundle's ordering signal (`TransformUpdate`)
      through the same mechanism so a module can express "after baseline" purely
      declaratively (coordinate with `BUG-069`).
- [x] Remove the now-redundant hand-rolled `b.Signal`/`b.WaitFor` calls from the
      contract-test `Setup` lambdas so the declarative fields are exercised on
      their own.

## Tests
- [x] A module declaring only `WaitForSignals`/`SignalLabels` (no manual builder
      calls) is correctly ordered per tick, including with `AllowParallel=true`.
- [x] `ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60` and default CPU gate.

## Docs
- [x] Update the feature-module playbook to state that declarative signal fields
      are the supported way to express per-tick order (no manual builder calls
      needed).

## Acceptance criteria
- [x] Declarative signal fields alone determine per-tick order.
- [x] Ordering no longer depends on `MainThreadOnly`/insertion order surviving.
- [x] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Closure verification on 2026-07-10:

- `IntrinsicRuntimeContractTests` built with the configured Clang `ci` preset.
- `RuntimeModuleSchedule.DeclarativeSignalsCreatePerTickEdgeForParallelSystems`
  registers its consumer before its producer, sets both passes to
  `MainThreadOnly=false` and `AllowParallel=true`, and uses only declarative
  labels. The compiled graph contains two ordered singleton layers and execution
  proves that the consumer observes the producer.
- The focused `RuntimeModule` selector passed 12/12. The default CPU-supported
  gate passed 3,657/3,657 in 431.23 s while unrelated compute workloads were
  active on the shared host.
- Strict layering, task policy, task-state links, documentation links, test
  layout, diff-mode docs synchronization, session-brief freshness, and
  whitespace checks passed.

## Completion

- Completed: 2026-07-10. Maturity: `CPUContracted`.
- Outcome: declarative signal fields are the single source of boot and per-tick
  module ordering, including for parallel-capable systems and the external
  baseline `TransformUpdate` producer.
- No `Operational` follow-up is owed: this is a backend-neutral CPU scheduling
  contract with deterministic execution-plan and runtime coverage.

## Forbidden changes
- Do not change core `FrameGraph` hazard orientation.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; no GPU or host-capability path is involved.

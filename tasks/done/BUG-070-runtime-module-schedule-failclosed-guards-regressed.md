---
id: BUG-070
theme: G
depends_on: []
---
# BUG-070 — RuntimeModule schedule dropped BUG-066 fail-closed guards

## Status
- Completed 2026-07-10 at `CPUContracted` on branch `main` in this workspace.
- Implementation commit: `7e77e47f`; the engine death regression,
  architecture note, and retirement synchronization are this local task commit.

## Goal
- Restore the `BUG-066` fail-closed guarantees that the recovery merge dropped:
  duplicate sim-system pass identities, cyclic dependencies, and unprovided
  signals produce recoverable, testable schedule errors rather than silently
  executing invalid passes. Engine boot explicitly translates those errors into
  its fail-closed initialization termination policy before any tick can run.

## Non-goals
- No change to `FinalizeForBoot`'s core canonical topological sort (that logic is
  correct and order-independent).
- No change to the named-signal ordering semantics.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.ModuleSchedule.*`,
  `src/runtime/Runtime.Engine.cpp`.
- Merge `76528e6` adopted the extraction-branch module schedule, which lacked
  the guards that `BUG-066` (commit `c0c2332`) had shipped and tested. `BUG-066`
  remained marked done, so the regression was invisible in task state.
- At discovery, `FinalizeForBoot` had no duplicate `(ModuleName, Name)` check;
  neither `Core.FrameGraph` nor `TaskGraph` deduplicates pass names, so both
  systems could execute silently. Unprovided signals and cycles called
  `std::terminate()` inside the schedule instead of returning `Core::Result`.
- The recovery merge also deleted `Test.RuntimeModuleContract.cpp` (with
  `DuplicateSystemPassNamesFailClosed` expecting `InvalidArgument` + zero passes,
  and `CyclicSystemSignalsFailClosed` expecting `InvalidState` + zero passes);
  its replacement initially covered order independence but none of these
  fail-closed cases.
- `7e77e47f` restored deterministic `InvalidArgument`/`InvalidState` results at
  the schedule seam. `Engine::Initialize()` retains the repository's global
  terminate-on-invalid-boot policy, now explicitly covered by a death test.

## Required changes
- [x] Detect duplicate `(ModuleName, Name)` sim-system identities in
      `FinalizeForBoot` (or at registration) and fail closed with a diagnostic,
      before any pass is appended.
- [x] Decide and implement the failure channel for duplicate/cycle/unprovided
      signal: prefer a recoverable `Core::Result` propagated to the boot caller
      (restoring `BUG-066`'s contract). If the boot path's `std::terminate`
      idiom is intentionally retained, make the intent explicit and cover it with
      death tests rather than leaving it untested.
- [x] Re-establish `BUG-066`'s deleted regression coverage in
      `Test.RuntimeModule.cpp` (or a sibling contract test).

## Tests
- [x] Duplicate identity fails closed (no duplicate pass executes).
- [x] Cyclic signal dependency fails closed.
- [x] Unprovided wait signal fails closed.
- [x] `ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60` and default CPU gate.

## Docs
- [x] Note the restored fail-closed contract in `docs/architecture/runtime.md`
      and reference this task from the `BUG-066` retirement note so the regression
      is recorded.

## Acceptance criteria
- [x] Two sim-systems with the same `(ModuleName, Name)` cannot both execute.
- [x] Duplicate/cycle/unprovided-signal failures are deterministic and covered by
      tests.
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
- Direct schedule contracts prove duplicate identities return
  `InvalidArgument`, while cycles and unprovided waits return `InvalidState`.
- `RuntimeModule.DuplicateSimSystemIdentityTerminatesEngineBootBeforeRun`
  registers the duplicate through the real `EngineSetup` path and proves
  initialization terminates before `Engine::Run()` can append or execute a
  fixed-step pass.
- The focused `RuntimeModule` selector passed 13/13. The complete default
  CPU-supported gate passed 3,658/3,658 in 432.35 s under concurrent host load.
- An earlier full-gate attempt was discarded after only the UV-atlas promotion
  timing smoke false-failed; its immediate isolated retry and this complete
  rerun passed. Repository policy requires that independently reproduced
  measurement-harness flake to be recorded as a separate bug next.
- Strict layering, task policy, task-state links, documentation links, test
  layout, diff-mode docs synchronization, session-brief freshness, and
  whitespace checks passed.

## Completion

- Completed: 2026-07-10. Maturity: `CPUContracted`.
- Outcome: invalid module schedules are rejected deterministically through a
  recoverable schedule result and cannot pass engine initialization.
- No task-specific `Operational` follow-up is owed; broader runtime-module
  composition maturity remains separately owned.

## Forbidden changes
- Do not make pass names imply producer/consumer direction.
- Do not weaken the order-independence guarantee already covered by
  `RegistrationOrderDoesNotChangeHooksOrSchedule`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; this is a backend-neutral CPU boot/schedule
  contract.

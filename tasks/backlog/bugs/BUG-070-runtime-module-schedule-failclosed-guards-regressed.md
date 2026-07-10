---
id: BUG-070
theme: G
depends_on: []
---
# BUG-070 — RuntimeModule schedule dropped BUG-066 fail-closed guards

## Goal
- Restore the `BUG-066` fail-closed guarantees that the recovery merge dropped:
  duplicate sim-system pass identities and cyclic signal dependencies must fail
  closed with a recoverable, testable error rather than silently double-executing
  or aborting the process.

## Non-goals
- No change to `FinalizeForBoot`'s core canonical topological sort (that logic is
  correct and order-independent).
- No change to the named-signal ordering semantics.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.ModuleSchedule.*`,
  `src/runtime/Runtime.Engine.cpp`.
- Merge `76528e6` adopted the extraction-branch module schedule, which lacks two
  guards that `BUG-066` (commit `c0c2332`) had shipped and tested. `BUG-066`
  remains marked done, so the regression is invisible in task state.
- Duplicate identity: `RuntimeModuleSchedule::FinalizeForBoot`
  (`Runtime.ModuleSchedule.cpp:81-180`) has no duplicate `(ModuleName, Name)`
  check, and neither `Core.FrameGraph` nor `TaskGraph::AddPassInternal`
  (`src/core/Core.Dag.TaskGraph.cpp:739-751`) dedupes pass names. Two sim-systems
  sharing an identity both become passes named `Module.Name`
  (`Runtime.ModuleSchedule.cpp:187-189`) and both execute per tick, silently. The
  duplicate-*module*-name guard (`Runtime.Engine.cpp:181-190`) does not cover two
  same-`Name` systems within one module. `BUG-066`'s `ApplySimSystems` returned
  `InvalidArgument` with zero passes for this case.
- Recoverable errors: `FinalizeForBoot` now calls `std::terminate()` for an
  unprovided wait signal (`:123`) and for a dependency cycle (`:168`), instead of
  returning a recoverable `Core::Result` (`InvalidState`) as `BUG-066` did. This
  is consistent with the boot path's terminate idiom but is untestable and
  abrupt.
- Lost coverage: `Test.RuntimeModuleContract.cpp` (with
  `DuplicateSystemPassNamesFailClosed` expecting `InvalidArgument` + zero passes,
  and `CyclicSystemSignalsFailClosed` expecting `InvalidState` + zero passes) was
  deleted; the replacement `Test.RuntimeModule.cpp` covers order-independence but
  not these fail-closed cases.

## Required changes
- [ ] Detect duplicate `(ModuleName, Name)` sim-system identities in
      `FinalizeForBoot` (or at registration) and fail closed with a diagnostic,
      before any pass is appended.
- [ ] Decide and implement the failure channel for duplicate/cycle/unprovided
      signal: prefer a recoverable `Core::Result` propagated to the boot caller
      (restoring `BUG-066`'s contract). If the boot path's `std::terminate`
      idiom is intentionally retained, make the intent explicit and cover it with
      death tests rather than leaving it untested.
- [ ] Re-establish `BUG-066`'s deleted regression coverage in
      `Test.RuntimeModule.cpp` (or a sibling contract test).

## Tests
- [ ] Duplicate identity fails closed (no duplicate pass executes).
- [ ] Cyclic signal dependency fails closed.
- [ ] Unprovided wait signal fails closed.
- [ ] `ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60` and default CPU gate.

## Docs
- [ ] Note the restored fail-closed contract in `docs/architecture/runtime.md`
      and reference this task from the `BUG-066` retirement note so the regression
      is recorded.

## Acceptance criteria
- [ ] Two sim-systems with the same `(ModuleName, Name)` cannot both execute.
- [ ] Duplicate/cycle/unprovided-signal failures are deterministic and covered by
      tests.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not make pass names imply producer/consumer direction.
- Do not weaken the order-independence guarantee already covered by
  `RegistrationOrderDoesNotChangeHooksOrSchedule`.
- Mixing mechanical file moves with semantic refactors.

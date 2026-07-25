---
id: BUG-120
theme: none
depends_on: []
---
# BUG-120 — Test.WorkflowConcurrency drifted from the CPU test sources it mirrors

## Goal
- Restore `tests/regression/tooling/Test.WorkflowConcurrency.py` to green by reconciling its
  expected multi-worker CTest `PROCESSORS` set and its config-root function names with the current
  CPU test sources.

## Non-goals
- Changing scheduler worker counts or CTest `PROCESSORS` reservations to satisfy the test.
- Relaxing the parity assertion into a subset check that would stop catching real drift.

## Context
- Observed while adding the ARA claim-ledger validator. All three failures reproduce on a clean
  checkout with no local modifications, so they are pre-existing and unrelated to that change.
- Three distinct drifts, all in the same direction — the test encodes a snapshot of test sources
  that have since moved:

  1. `test_cpu_engine_config_roots_use_one_worker` cannot find `PoolConfig` in
     `tests/contract/runtime/Test.RenderWorldPoolEngineWiring.cpp`.
  2. The same test cannot find `SingleWorkerEngineConfig` in
     `tests/contract/runtime/Test.RuntimeReferenceScene.cpp`.
  3. `test_exact_multiworker_ctest_budgets_match_cpu_sources` reports a symmetric difference in the
     `(target, case, processors)` set. Present in sources but not expected: four
     `IntrinsicCoreWrapperUnitTests` cases
     (`CoreTasks.WorkProgressTokenFromPriorSchedulerInstanceFailsClosed` at 3,
     `CoreTaskGraphCompletionLifetime.NonOwnerCanPollCopiedCompletionUntilReady` at 4,
     `CoreTaskGraphCompletionLifetime.WaitWakesForChildDispatchedAfterHelperRegisters` at 3,
     `CoreTasks.WaitForAllWakesForChildDispatchedAfterHelperRegisters` at 3). Expected but not
     present: `IntrinsicGraphicsContractCpuTests`
     `RendererFrameLifecycle.NativeGpuProfilerUsesAcceptedParallelMultiQueueAttribution` at 4 and
     `CoreTaskGraphCompletionLifetime.NonOwnerCanPollCopiedCompletionUntilReady` at 3.
- Note that `CoreTaskGraphCompletionLifetime.NonOwnerCanPollCopiedCompletionUntilReady` appears on
  both sides with different reservations (4 in sources, 3 expected), so at least one reservation
  genuinely changed rather than a case merely being added or renamed. Decide per case whether the
  source reservation or the expectation is correct; `AGENTS.md` §7 requires matched sanitizer
  evidence before raising a CTest parallel budget, so a raised reservation needs that evidence.

Reproduction on a clean tree:

```
$ git stash push -u && python3 tests/regression/tooling/Test.WorkflowConcurrency.py
Ran 19 tests — FAILED (failures=3)
```

## Required changes
- [ ] Reconcile the two config-root function names with the current contract test sources.
- [ ] Reconcile the multi-worker `(target, case, processors)` expectation per case, deciding for
      each whether the source or the expectation is authoritative.
- [ ] If any reservation legitimately increased, record the matched sanitizer evidence required by
      `AGENTS.md` §7 alongside the change.

## Tests
- [ ] `python3 tests/regression/tooling/Test.WorkflowConcurrency.py` passes on a clean checkout.
- [ ] The parity assertion still fails when a case's `PROCESSORS` reservation is changed without
      updating the expectation.

## Docs
- [ ] If a reservation changes, update the relevant note in `tests/README.md` or the CI docs.

## Acceptance criteria
- [ ] All 19 tests green, with the parity check still symmetric (not subset-relaxed).
- [ ] No scheduler worker count or CTest budget changed merely to satisfy the test.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tests/regression/tooling/Test.TestCohortParity.py
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

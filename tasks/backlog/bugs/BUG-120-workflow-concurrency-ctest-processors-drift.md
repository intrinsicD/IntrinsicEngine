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
### Ground truth (resolved by reading the sources)

`declared` comes from `_intrinsic_multiworker_test_budgets` in `tests/CMakeLists.txt`;
`source_budgets` comes from `_source_multiworker_budgets()` scanning the C++ for
`Scheduler::Initialize(N)` / `SchedulerScope`/`SchedulerFixture{N}`, converting to slots via
`_scheduler_peak_slots()` (workers + 1) and dropping anything with `N <= 1`.

Reading the four disputed cases settles every one of them — the CMake list is stale, and the
sources are authoritative:

| Case | Source | CMake | Correct action |
|---|---|---|---|
| `CoreTaskGraphCompletionLifetime.NonOwnerCanPollCopiedCompletionUntilReady` | `SchedulerFixture scheduler{2}` → 3 slots | 4 | lower to 3 |
| `CoreTaskGraphCompletionLifetime.WaitWakesForChildDispatchedAfterHelperRegisters` | `SchedulerFixture scheduler{1}` → single-worker | 3 | remove |
| `CoreTasks.WaitForAllWakesForChildDispatchedAfterHelperRegisters` | `Scheduler::Initialize(1)` | 3 | remove |
| `CoreTasks.WorkProgressTokenFromPriorSchedulerInstanceFailsClosed` | `Scheduler::Initialize(1)` | 3 | remove |
| `IntrinsicGraphicsContractCpuTests` `RendererFrameLifecycle.NativeGpuProfilerUsesAcceptedParallelMultiQueueAttribution` | 4 slots | absent | add at 4 |

**No `AGENTS.md` §7 sanitizer evidence is owed.** An earlier reading of the set difference
suggested a reservation had been *raised* in the sources; it had not. CMake over-declared 4 where
the fixture asks for 2 workers, and three cases became single-worker so they need no multiworker
reservation at all. The only addition (the graphics case at 4) declares a reservation that the
source already requires, which is the safe direction. Nothing here raises a budget beyond what a
test actually spawns.

The aggregate guards in the same test must move with the list:
`self.assertEqual(len(declared), 43)` → **40**, and the distribution
`{3: 22, 4: 19, 8: 2}` → **`{3: 20, 4: 19, 8: 2}`** (−3 removals at 3, one 4→3 move, one +1 at 4).

Reproduction on a clean tree:

```
$ git stash push -u && python3 tests/regression/tooling/Test.WorkflowConcurrency.py
Ran 19 tests — FAILED (failures=3)
```

## Required changes
- [ ] Reconcile the two config-root function names with the current contract test sources
      (`PoolConfig` in `Test.RenderWorldPoolEngineWiring.cpp`, `SingleWorkerEngineConfig` in
      `Test.RuntimeReferenceScene.cpp`) — find their current names or the constructs that replaced
      them, and update `CPU_ENGINE_CONFIG_ROOTS`.
- [ ] Apply the five `_intrinsic_multiworker_test_budgets` corrections in the ground-truth table
      above to `tests/CMakeLists.txt`.
- [ ] Update the two aggregate guards in `test_exact_multiworker_ctest_budgets_match_cpu_sources`
      to `len == 40` and `{3: 20, 4: 19, 8: 2}`.
- [ ] Do not raise any reservation above what its test actually spawns; if that ever becomes
      necessary, `AGENTS.md` §7 requires matched sanitizer evidence first (not applicable to the
      corrections above).

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

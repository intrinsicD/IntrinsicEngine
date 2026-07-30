---
id: BUG-120
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-07-30T11:50:38Z"
---
# BUG-120 — Test.WorkflowConcurrency drifted from the CPU test sources it mirrors

## Status
- Completed: 2026-07-30.
- Commit: `62e232ce1a0757914b5311633afec58725f83cd0`.
- Maturity: `CPUContracted`; this is a CPU/tooling parity contract and no
  `Operational` backend follow-up is owed.
- The clean-tree reproduction at `e217c3a6` fails 4 of 19 cases.
- The corrected regression passes 19/19, the cohort-parity suite passes 7/7,
  and the canonical `ci` configure plus `IntrinsicTests` build succeeds.

## Goal
- Restore `tests/regression/tooling/Test.WorkflowConcurrency.py` to green by reconciling its
  expected multi-worker CTest `PROCESSORS` set and its config-root function names with the current
  CPU test sources.

## Non-goals
- Changing scheduler worker counts or CTest `PROCESSORS` reservations to satisfy the test.
- Relaxing the parity assertion into a subset check that would stop catching real drift.

## Context
- The original three-failure snapshot was recorded before `RUNTIME-201` added
  more multi-worker runtime cases. On clean `main` at `e217c3a6`, the focused
  regression now fails four of 19 tests; this task records the current source
  truth rather than preserving its stale intermediate counts.
- The failures remain snapshot drift rather than production scheduler defects:

  1. `tests/contract/runtime/Test.RenderWorldPoolEngineWiring.cpp` is now a
     structural source-inspection test and no longer constructs an
     `EngineConfig`; remove its obsolete `PoolConfig` root.
  2. `tests/contract/runtime/Test.RuntimeReferenceScene.cpp` now owns the same
     one-worker configuration in `HeadlessConfig`, replacing
     `SingleWorkerEngineConfig`.
  3. The default/explicit `NullWindowHeadlessConfig` call inventory in
     `Test.ClusteringModule.cpp` is now six default multi-worker calls and
     three explicit single-worker calls.
  4. The manual CTest list contains 57 entries with
     `{3: 33, 4: 22, 8: 2}`, while the source scanner derives 73 entries with
     `{3: 49, 4: 22, 8: 2}`.

### Ground truth (resolved by reading the sources)

`declared` comes from `_intrinsic_multiworker_test_budgets` in `tests/CMakeLists.txt`;
`source_budgets` comes from `_source_multiworker_budgets()` scanning the C++ for
`Scheduler::Initialize(N)` / `SchedulerScope`/`SchedulerFixture{N}`, converting to slots via
`_scheduler_peak_slots()` (workers + 1) and dropping anything with `N <= 1`.

The source-derived symmetric difference has four stale declarations and 20
missing declarations. One missing declaration is the corrected three-slot
form of a stale four-slot declaration:

| Case | Source | CMake | Correct action |
|---|---|---|---|
| `CoreTaskGraphCompletionLifetime.NonOwnerCanPollCopiedCompletionUntilReady` | `SchedulerFixture scheduler{2}` → 3 slots | 4 | lower to 3 |
| `CoreTaskGraphCompletionLifetime.WaitWakesForChildDispatchedAfterHelperRegisters` | `SchedulerFixture scheduler{1}` → single-worker | 3 | remove |
| `CoreTasks.WaitForAllWakesForChildDispatchedAfterHelperRegisters` | `Scheduler::Initialize(1)` | 3 | remove |
| `CoreTasks.WorkProgressTokenFromPriorSchedulerInstanceFailsClosed` | `Scheduler::Initialize(1)` | 3 | remove |
| `IntrinsicGraphicsContractCpuTests` `RendererFrameLifecycle.NativeGpuProfilerUsesAcceptedParallelMultiQueueAttribution` | 4 slots | absent | add at 4 |

The remaining 19 additions mirror current source requests: 14
`RuntimeJobService` cases and four `ClusteringModule` cases at three slots,
plus the graphics profiler case above at four slots. They do not raise any
reservation above what the corresponding test already spawns.

**No `AGENTS.md` §7 sanitizer evidence is owed.** The changes only remove
over-declarations, lower one reservation, or declare the peak capacity already
requested by current sources. No test worker count changes and no reservation
is raised beyond its source request.

Reproduction on a clean tree:

```
$ python3 tests/regression/tooling/Test.WorkflowConcurrency.py
Ran 19 tests — FAILED (failures=4)
```

## Required changes
- [x] Remove the obsolete `PoolConfig` root, rename
      `SingleWorkerEngineConfig` to `HeadlessConfig`, and update the clustering
      default/explicit call-count guards.
- [x] Reconcile `_intrinsic_multiworker_test_budgets` exactly to the
      source-derived 73-case set in `tests/CMakeLists.txt`.
- [x] Update the aggregate guards in
      `test_exact_multiworker_ctest_budgets_match_cpu_sources` to `len == 73`
      and `{3: 49, 4: 22, 8: 2}`.
- [x] Do not raise any reservation above what its test actually spawns; if that ever becomes
      necessary, `AGENTS.md` §7 requires matched sanitizer evidence first (not applicable to the
      corrections above).

## Tests
- [x] `python3 tests/regression/tooling/Test.WorkflowConcurrency.py` passes on a clean checkout.
- [x] The parity assertion still fails when a case's `PROCESSORS` reservation is changed without
      updating the expectation.

## Docs
- [x] Update the current-state reservation inventory in `tests/README.md`;
      preserve the separately identified historical CI-008 counts in
      `docs/benchmarking/ci-policy.md`.

## Acceptance criteria
- [x] All 19 tests green, with the parity check still symmetric (not subset-relaxed).
- [x] No scheduler worker count or CTest budget changed merely to satisfy the test.

## Verification
```bash
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tests/regression/tooling/Test.TestCohortParity.py
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

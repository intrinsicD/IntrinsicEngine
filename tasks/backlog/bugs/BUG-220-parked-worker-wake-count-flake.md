---
id: BUG-220
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Intermittent test-reliability incident; no performance or capability claim.
contract_schema: 1
contracts: []
contract_review: Existing scheduler test/verification hygiene applies; no new engine contract.
---
# BUG-220 — Parked-worker wake count flakes under parallel CTest load

## Goal
Make `CoreTasks.ParkedWorkerDispatchHandshakeMakesRepeatedProgress` deterministic
under a loaded full CPU run, either by fixing a real lost-notification path in
`Scheduler::Dispatch` or by asserting only what the park/signal protocol
guarantees. Do not weaken the progress guarantee the test exists for.

## Evidence
On 2026-09-25, during the full CPU gate for the ScalarfieldExtrema/Segmentation
merge (`ee60fc40e`, `ctest -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j8`,
5031/5032 passed), the test failed once; `src/core` was not touched:

```text
tests/unit/core/Test.CoreTasks.cpp:263: Failure
Expected equality of these values:
  stats.WorkerWakeNotifications
    Which is: 511
  wakeNotificationsBefore + static_cast<std::uint64_t>(iterationCount)
    Which is: 512
```

All 512 dispatches completed; one dispatch recorded no wake notification.
The test passed 21 consecutive isolated runs (`--repeat until-fail:20` plus one).

## Hypothesis (unverified)
`Dispatch` counts a notification only when it observes `parkedWorkerCount != 0`
(`src/core/Core.Tasks.Dispatch.cpp`). The test waits for `ParkedWorkers == 1`
before dispatching, but a worker that leaves its park wait (spurious wake or
signal recheck) between that observation and the dispatch's load is picked up
by the recheck path without a counted notification. If so, progress is intact
and the exact-count assertion overstates the protocol; otherwise a notification
path is racing.

## Resolution — 2026-10-24

**Root cause: no lost wakeup, the exact count was overstated.** The worker
(`Core.Tasks.Worker.cpp:59-72`) publishes its park P (`parkedWorkerCount++`),
rechecks `workSignal` R, calls `std::atomic::wait`, then retracts U. `Dispatch`
(`Core.Tasks.Dispatch.cpp:116-122`) bumps the signal S, then loads the parked
count L and counts and calls `notify_one` only if L sees a park. All are seq_cst.
No wakeup is lost: R sees S, or the wait's own value check sees S before U, or
L sees the park and notifies. The counter only counts dispatches whose L still
sees the park. A worker in the libstdc++ pre-sleep spin, or before R, takes S
and runs U before L; the task completes, but that dispatch is not counted. The
hypothesis holds in its "progress intact" branch.

Forced interleavings (gdb, scheduler-locking) on the pre-change test reproduced
the incident:

| Variant | Forced order | Worker | L saw | Notifications | Completed |
|---|---|---|---|---|---|
| A | P, R(equal), S, wait returns from spin, U, L | `waited=true` | 0 | 511 | 512 |
| B | P, S, R(new signal), U, L | `waited=false` | 0 | 511 | 512 |
| C | P, S, R(skip sleep), L, U | `waited=false` | 1 | 512 | 512 |

Variant C was counted although the worker never slept. A and C together rule
out the identity "notifications + recheck pickups == iterations" (511 vs 513),
so no production counter was added. Commits `a222e089c` and `b7a373a61`
(2026-10-02, no task ID) had already loosened the check to
`0 < notifications <= 512`, with a 20 ms sleep. That version forced only one
sleeping-worker notify per run.

**Change (test only).** In `CoreTasks.ParkedWorkerDispatchHandshakeMakesRepeatedProgress`,
each dispatch now waits in `WaitUntilWorkerSleepsInPark`. The wait ends when
`ParkedWorkers == 1` and `/proc/self/task/<tid>/syscall` shows `SYS_futex`; it
uses the probe from `Test.CoreProcess.cpp`. The exact
`EXPECT_EQ(notifications, 512)` is back, and the 20 ms sleep is gone. The gate
is sound only while no signal is pending:

- the worker can leave P..U only after a new S;
- inside P..U, the park futex is the only syscall that blocks.

The worker tid is therefore captured by a task dispatched *after* the first
published park, before any other dispatch. That worker sees S_tid before it
unparks, so no stale park survives. Every gated dispatch is consumed before the
worker parks again. Production code is unchanged.

Defaults chosen in delegate mode:

- Option B (test-only), not a scheduler change.
- Gate deadline of 5 s. It turns a hang into the failure "worker never slept in
  the park futex before iteration N" (precedent: the 2 s deadline in
  `Test.CoreProcess.cpp`). No run tuned it.
- No separate pre-baseline gate. With the reordered tid capture there is no
  stale-park state to flush; the baseline is a main-thread counter read.
- The probe stays local: about 10 lines, used by one other test executable.
  jscpd on `tests/unit/core` and `tests/integration/runtime`: no clone with the
  CoreProcess probe. The only overlap is a generic 5-line deadline loop shared
  with a GPU smoke test.
- `EXPECT_NE(tid, main tid)` guards against capturing the wrong thread.

**Regression proofs.** Each prediction was fixed before its run. Logs are in
`tasks/evidence/BUG-220/` (git-ignored).

1. Mutation m0 removes the futex check; the gate then waits only for
   `ParkedWorkers == 1`. The script forces variants A and B at iteration 0 on the
   new test. Result as predicted: `Test.CoreTasks.cpp:375: Failure ... notifications
   Which is: 511 ... 512`, with `completed == 512` in both variants.
2. With the gate in place, the worker is held at `Worker.cpp:68` (A) or `:62` (B)
   before iteration 0 and only main runs. As predicted, main never reached the
   S line `Dispatch.cpp:116` (`workSignal` stayed 1). After the deadline it hit
   the gate failure at `Test.CoreTasks.cpp:351` with `iteration == 0`.
3. Mutation m1 deletes `workSignal.notify_one()` in `Dispatch.cpp`. Predicted:
   the test fails. Observed: ctest `***Timeout 30.04 sec`; the test's 30 s
   TIMEOUT property overrides `--timeout 60`. The mutation was reverted and the
   test rebuilt.

**gdb recipe** (Debug `build/ci`, line numbers as of this change):

1. Break at `Test.CoreTasks.cpp:339` (body of the tid-capture task), run
   `--gtest_filter=CoreTasks.ParkedWorkerDispatchHandshakeMakesRepeatedProgress`,
   then `set scheduler-locking on`.
2. On the worker, `tbreak Core.Tasks.Worker.cpp:68` (A: R equal, before wait) or
   `:62` (B: before R), then `continue`.
3. On thread 1, `tbreak Core.Tasks.Dispatch.cpp:117` (S done, L pending), then
   `continue`.
4. On the worker, `tbreak Core.Tasks.Worker.cpp:74` (U done), then `continue`.
5. On thread 1, `tbreak Core.Tasks.Dispatch.cpp:123` (after L), then `continue`.
6. `set scheduler-locking off`, break at `Test.CoreTasks.cpp:375`, and print
   `notifications`.
7. For C, use `Worker.cpp:62`, then `:71` before L, and let U happen after L.
8. `$ctx` is `*(Extrinsic::Core::Tasks::Detail::SchedulerContext**)&'_ZN9Extrinsic4Core5TasksW9ExtrinsicW4CoreW5Tasks5s_CtxE'`.

**Verification on the final test** (32-core host, other lanes compiling):

- `nice -n 10 cmake --build --preset ci --parallel 3 --target IntrinsicCoreWrapperUnitTests`: OK.
- `ctest --test-dir build/ci -R 'CoreTasks\.' --repeat until-fail:200 --timeout 60`:
  34/34 passed, 107 s, load 3.6 → 3.7.
- `ctest --test-dir build/ci -R 'CoreTasks[.]' --repeat until-fail:200 --timeout 60 -j8`:
  34/34 passed, two runs (36 s each), load 3.5 → 4.3 and 12.3 → 10.3.
- `ctest --test-dir build/ci -R '^CoreTasks\.ParkedWorkerDispatchHandshakeMakesRepeatedProgress$' --repeat until-fail:1000 --timeout 60`:
  passed, 21 s, load 17.4 → 12.3.
- Preemption stress: 8 concurrent processes with `--gtest_repeat`, pinned by
  `taskset` (adds no host CPU). On CPUs 30-31, 2000/2000 passed. On CPU 31
  alone, 8000/8000 passed (268 s). No gate-deadline failures.

**AC3 is not demonstrated under incident-like load.** The rule was set before
the runs: if the load was light, AC3 would be reported as not demonstrated.

- The host stayed lightly loaded (load average ≤ 23 on 32 cores, with most
  of it from the pinned stress itself).
- The load runs also discriminate weakly. The m0 mutant without the gate never
  failed naturally: 300 isolated runs, 2000 on 2 pinned CPUs, and 8000 on 1
  pinned CPU.
- The reproduction is therefore the deterministic gdb one, not a load one.
  Reviewers decide whether that meets AC1/AC3.

**Not run:** the full CPU gate from Verification
(`ctest ... -LE 'gpu|vulkan|slow|flaky-quarantine' -j8`). The change touches one
test source: no `.cppm` and no production code. AGENTS.md section 7 therefore
calls for focused verification; PR/merge CI runs the full gate.

**Remaining risks:**

- The test is Linux-only (procfs and `SYS_futex`); CI is Linux-only.
- A signal or a kernel spurious futex wake after S could still make one
  dispatch uncounted. None is expected under ctest.
- If a future `std::atomic::wait` blocks without a futex, the gate fails with
  the deadline message instead of hanging.

## Acceptance criteria
- [ ] Reproduce under load (e.g. repeated runs alongside a parallel CTest) and confirm or refute the hypothesis.
- [ ] Fix the scheduler or the assertion; the test still proves repeated progress of a parked worker.
- [ ] The test passes repeatedly under the loaded reproduction.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -R 'CoreTasks\.' --repeat until-fail:200 --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j8
```

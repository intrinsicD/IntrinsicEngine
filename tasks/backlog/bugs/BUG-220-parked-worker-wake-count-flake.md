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
each dispatch first waits in `WaitUntilWorkerSleepsInPark`. The wait ends when
`ParkedWorkers == 1` and `/proc/self/task/<tid>/syscall` shows `SYS_futex`; it
uses the probe from `Test.CoreProcess.cpp`. The 20 ms sleep is gone. Each
dispatch is then classified by its own notification delta: counted (1) or
uncounted (0). The loop runs until 512 dispatches are counted and asserts:

- at most one notification per dispatch;
- `counted == 512` and `uncounted <= 1` (a second uncounted dispatch ends the loop);
- `completed == counted + uncounted`.

Why the gate is sound:

- The worker tid is captured by a task dispatched *after* the first published
  park, before any other dispatch, so no stale park survives.
- While no signal is pending the worker cannot leave P..U, and the worker
  consumes every gated dispatch before it parks again.
- `/proc/<tid>/syscall` reports a syscall number only while the task is not
  running, and `futex_wait` sleeps only after its value check under the
  hash-bucket lock. With a published park, `SYS_futex` therefore means the
  worker is queued in the park futex, past its value check.

Why one uncounted dispatch is tolerated (finding 1 of the second round-0 independent
review). A queued worker can still leave the futex without the notify:

- a signal or ptrace stop restarts the futex call;
- a spurious kernel wakeup makes `futex_wait` retry.

Both re-check the value. After S, the call returns to user space and the worker
runs U before L. The protocol is still correct, because the worker saw S, but
the dispatch is uncounted. This needs a wake from outside the protocol inside
the few instructions between S and L. A second such dispatch in one run fails
the test, and so does a systematic miss, at its second dispatch.

Defaults chosen in delegate mode:

- Option B (test-only), not a scheduler change.
- Gate deadline of 5 s. It turns a hang into the failure "worker never slept in
  the park futex before dispatch N" (precedent: the 2 s deadline in
  `Test.CoreProcess.cpp`). No run tuned it.
- Tolerance of one uncounted dispatch per run. No run chose this number. It is
  structural: one outside wake cannot fail the test, two can. Alternatives are
  listed for the operator.
- The probe stays local: about 10 lines, used by one other test executable.
  jscpd on `tests/unit/core` and `tests/integration/runtime`: no clone with the
  CoreProcess probe. The only overlap is a generic 5-line deadline loop shared
  with a GPU smoke test.
- `EXPECT_NE(tid, main tid)` guards against capturing the wrong thread.

**Regression proofs.** Each prediction was fixed before its run, and every result
matched. Logs and gdb scripts are in `tasks/evidence/BUG-220/` (git-ignored).

Round 0 (`15c9fb5ea` and its mutants):

1. Mutation m0 removes the futex check, so the gate waits only for
   `ParkedWorkers == 1`. Variants A and B were forced at iteration 0, and the
   exact assertion failed with 511 vs 512, `completed == 512` (`nogate-*.log`).
2. With the gate in place, the worker was held at `Worker.cpp:68` (A) or `:62`
   (B) before iteration 0. Main never reached S, and the gate deadline failure
   followed at iteration 0 (`gate-hold-*.log`). The gate function is unchanged
   in round 1.
3. Mutation m1 deletes `workSignal.notify_one()`: ctest `***Timeout 30.04 sec`.

Round 1 (this change):

4. **Variant D** is the review sequence. It was forced on the committed round-0
   test: the gate passes while the worker is in the `__platform_wait` futex
   syscall, and main runs S. The gdb all-stop had interrupted the futex. Resumed
   alone, the worker restarts it, sees S, returns with `waited=true` and runs U.
   Main's L then sees 0. Result: `Test.CoreTasks.cpp:375: Failure ... 511 ... 512`,
   `completed == 512` (`force-escape-D-head.log`). This reproduces the review
   sequence, with the ptrace stop as the outside wake.
5. D once on the round-1 test: pass, with counted 512, uncounted 1, completed
   513 (`force-escape-D1-new.log`).
6. D at dispatches 0 and 1: fail at `:385` (counted 0 vs 512) and `:386`
   (uncounted 2 vs 1), completed 2 (`force-escape-D2-new.log`).
7. m0 with variant A forced before dispatches 0 and 1: the same two failures,
   counted 0, uncounted 2 (`round1-m0-twice-A.log`).
8. m2 deletes the `workerWakeNotificationCount` increment in `Dispatch.cpp`. The
   test fails in 9 ms at `:385` (0 vs 512) and `:386` (2 vs 1)
   (`round1-m2-run.log`).
9. m1 on the round-1 test: ctest `***Timeout 30.01 sec` (`round1-m1-run.log`).

All mutations were reverted. The final binary was rebuilt from the final test
source (byte-compared with `round1-final-Test.CoreTasks.cpp`) and the unchanged
`Dispatch.cpp`.

**gdb recipe** (Debug `build/ci`, round-1 line numbers):

- `$ctx` is `*(Extrinsic::Core::Tasks::Detail::SchedulerContext**)&'_ZN9Extrinsic4Core5TasksW9ExtrinsicW4CoreW5Tasks5s_CtxE'`.
- **A/B/C.** Break at `Test.CoreTasks.cpp:343` (tid-capture task body), run
  `--gtest_filter=CoreTasks.ParkedWorkerDispatchHandshakeMakesRepeatedProgress`,
  then `set scheduler-locking on`. Step each thread with tbreak plus continue,
  in this order:
  1. worker to `Core.Tasks.Worker.cpp:68` (A) or `:62` (B);
  2. thread 1 to `Core.Tasks.Dispatch.cpp:117` (S done);
  3. worker to `Worker.cpp:74` (U done);
  4. thread 1 to `Dispatch.cpp:123` (after L).

  For C, stop the worker at `:62`, then at `:71` before L, and let U run after L.
- **D.** Break at `Test.CoreTasks.cpp:361` (gate passed), then
  `set scheduler-locking on`. Run thread 1 to `Dispatch.cpp:117`, the worker to
  `Worker.cpp:74`, and thread 1 to `Dispatch.cpp:123`.
- To read the result, run `set scheduler-locking off`, break at
  `Test.CoreTasks.cpp:385`, and print `counted` and `uncounted`.

**Verification of the round-1 test** (32-core host, other lanes mostly idle):

- `nice -n 10 cmake --build --preset ci --parallel 3 --target IntrinsicCoreWrapperUnitTests`: OK.
- `ctest --test-dir build/ci -R 'CoreTasks\.' --repeat until-fail:200 --timeout 60`:
  34/34 passed in 103 s. The load went from 1.2 to 14.4, from the concurrent
  pinned stress.
- `ctest --test-dir build/ci -R 'CoreTasks[.]' --repeat until-fail:200 --timeout 60 -j8`:
  34/34 passed in 36 s, load 14.7 → 17.0.
- `ctest --test-dir build/ci -R '^CoreTasks\.ParkedWorkerDispatchHandshakeMakesRepeatedProgress$' --repeat until-fail:1000 --timeout 60`:
  passed in 21 s.
- Preemption stress (`pinned-stress.sh`, `CPUS=31`): 8 concurrent processes
  ran `--gtest_repeat=1000`, all `taskset` to CPU 31. 8000/8000 passed in
  288 s, with no gate-deadline failures.

**AC3 is not demonstrated under incident-like load.** The rule was set before
the runs: if the load was light, AC3 would be reported as not demonstrated.

- The host stayed lightly loaded (load average ≤ 23 on 32 cores, mostly from
  the pinned stress itself).
- The load runs discriminate weakly. In round 0 the m0 mutant without the gate
  never failed naturally: 300 isolated runs, 2000 on 2 pinned CPUs and 8000 on
  1 pinned CPU.
- The reproduction is therefore the deterministic gdb one, not a load one.
  Reviewers or the operator decide whether that meets AC1/AC3.

**Not run:** the full CPU gate from Verification
(`ctest ... -LE 'gpu|vulkan|slow|flaky-quarantine' -j8`). The change touches one
test source: no `.cppm` and no production code. AGENTS.md section 7 therefore
calls for focused verification; PR/merge CI runs the full gate.

**Remaining risks:**

- The test is Linux-only (procfs and `SYS_futex`); CI is Linux-only.
- Two outside wakes inside S..L in one run fail the test. This happens, for
  example, when a debugger in all-stop mode stops the process during two
  dispatches (proof 6). It is not expected under ctest.
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

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

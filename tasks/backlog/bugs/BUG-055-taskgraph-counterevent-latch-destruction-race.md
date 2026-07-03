---
id: BUG-055
theme: G
depends_on: []
---
# BUG-055 — TaskGraph::Execute / CounterEvent latch-destruction race

## Goal
- Make `TaskGraph::Execute()` completion signaling safe: no worker thread may
  touch the graph's stack-local `ExecutionState` (its `CounterEvent`, its
  `onTaskFinished`/`scheduleReadyBatch` `std::function`s, or its atomics)
  after the waiting caller has been released and the state destroyed.

## Non-goals
- No new submit/poll/completion-token execution API — owned by `CORE-005`.
- No scheduler queue/priority redesign — owned by `CORE-007`.
- No change to `CounterEvent`'s public contract beyond destruction safety.

## Context
- Owner/layer: `core` (`Extrinsic.Core.Dag.TaskGraph`,
  `Extrinsic.Core.Tasks.CounterEvent`).
- Symptom: `TaskGraph::Execute` waits with `while (!state.Done.IsReady())` on
  a stack-local `ExecutionState` (`src/core/Core.Dag.TaskGraph.cpp:999`). The
  worker finishing the final pass calls `state.Done.Signal()`;
  `CounterEvent::Signal` CASes `m_Count` to zero and *then* reads
  `this->m_Token` to call `Scheduler::UnparkReady`
  (`src/core/Core.Tasks.CounterEvent.cpp:40-45`). The moment the CAS lands,
  the caller can observe readiness, return from `Execute`, and destroy
  `state` — while the worker is still inside `Signal` (use-after-free read of
  `m_Token`, plus `~CounterEvent` releasing the token mid-use) and still
  inside the stack-captured `onTaskFinished` lambda body.
- Expected behavior: `Execute` returns only after every worker has fully
  retired its completion path; signaling never touches destroyed state.
- Impact: rare memory corruption / UB on every fixed-step sim tick and every
  `Graphics.RenderPrepPipeline` run (both execute graphs through this path).
  Window is narrow (CAS→unpark), so it will surface as unreproducible
  crashes under load or sanitizers.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R1 (verified by direct source read).

## Required changes
- [ ] Restructure `Execute` so completion state outlives every dispatched
      task's full completion path — e.g. `std::shared_ptr<ExecutionState>`
      owned by each dispatched closure (moving `onTaskFinished`/
      `scheduleReadyBatch` into the shared state, not stack captures), or an
      explicit signals-retired fence (`in-flight completion` counter drained
      before return). Pick the smaller, testable option and document why.
- [ ] Harden `CounterEvent::Signal` to capture everything it needs before the
      CAS that can release a waiter, and document that `Signal` reaching zero
      must be the last access to the event by that thread — insufficient
      alone (the enclosing lambda frame must also outlive the wake), but
      required so the event type is not a trap for the next caller.
- [ ] Audit the other `CounterEvent` wait/destroy sites
      (`Core.Tasks.WaitToken.cpp`, `Asset.LoadPipeline.cpp`) for the same
      signal-then-touch pattern; fix or record N/A per site in this file.

## Tests
- [ ] Stress regression: repeated small-graph `Execute` across many
      iterations with worker passes racing completion (TSan-friendly; must
      fail reliably under ThreadSanitizer before the fix, pass after).
- [ ] Existing `CoreTaskGraph.*` suite stays green, including
      `MainThreadReadyQueueUsesPriorityAndCostOrdering` (BUG-046 history).
- [ ] `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`

## Docs
- [ ] Note the completion-lifetime contract in `src/core/README.md` (who may
      touch execution state after the final signal).

## Acceptance criteria
- [ ] ThreadSanitizer run of the new stress regression is clean.
- [ ] No worker thread accesses `ExecutionState` or the `CounterEvent` after
      `Execute` returns (proven by the restructure, not by timing).
- [ ] Default CPU gate green; no layering changes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# TSan configuration (or equivalent sanitizer preset) for the stress regression:
ctest --test-dir build/ci --output-on-failure -R 'CoreTaskGraph' --repeat until-fail:50 --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Shipping the fix without a race-focused regression test.
- Changing `TaskGraph` public API shape (that is `CORE-005`).
- "Fixing" the race with sleeps, yields, or timing margins.

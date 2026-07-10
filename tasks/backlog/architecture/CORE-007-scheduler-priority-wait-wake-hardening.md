---
id: CORE-007
theme: F
depends_on: []
---
# CORE-007 — Scheduler priority, wait, and wake hardening

## Goal
- Close the efficiency and scheduling gaps in `Extrinsic.Core.Tasks`: task
  priorities that survive into dispatch, external waiters that can steal,
  wakeups only when a worker is parked, and cheaper hot-path bookkeeping.

## Non-goals
- No graph-level API changes — `CORE-005` (submit) and `CORE-008` (plan
  cache) own those.
- No fairness/latency guarantees beyond "higher priority is preferred under
  contention"; this is not a realtime scheduler.
- A Chase-Lev lock-free deque rewrite is optional scope: measure first; keep
  the spin-locked deque if the probe shows it is not a bottleneck, and
  record the decision here.

## Context
- Owner/layer: `core` (`Core.Tasks.*`).
- Current gaps (origin:
  `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R14, plus scheduler findings 5/8/10/11):
  - `Scheduler::Dispatch` has no priority parameter
    (`src/core/Core.Tasks.cppm:60-64`), so
    `TaskGraphPassOptions::Priority` is lost for worker-eligible passes —
    `Critical` vs `Background` matters exactly under contention.
  - `WaitForAll` help-runs only the inject queue
    (`Core.Tasks.Lifecycle.cpp:67`, `Dispatch.cpp:80-92`); tasks in
    worker-local deques force the waiter to sleep instead of stealing.
  - Every dispatch does `workSignal.fetch_add + notify_one` and every
    `SpinLock::unlock` does `notify_one` (`Dispatch.cpp:63-64`,
    `Internal.cpp:52`) — potential futex syscalls with no parked waiter.
  - `TaskGraph::Execute` calls `GetStats()` (locks every worker queue) just
    to count workers (`Core.Dag.TaskGraph.cpp:893`).
  - One global `waitMutex` serializes all parks/unparks/token ops across
    the process (`Core.Tasks.Internal.cppm:103-107`).
  - Parallel execute allocates `std::function` wrappers and fresh vectors
    per pass completion (`Core.Dag.TaskGraph.cpp:916-922, 978-979`).
- ARCH-013 re-review (2026-07-08): Decision unchanged. Scheduler priority,
  stealing, wake, and scratch improvements stay below `JobService` and the
  frame-graph split; priority lanes must remain generic scheduler policy, not
  runtime domain queues.

## Required changes
- [ ] Add a small fixed set of priority lanes to external dispatch (e.g.
      High/Normal/Low inject queues, higher lanes drained first); thread
      `TaskGraphPassOptions::Priority` through worker-eligible pass dispatch.
- [ ] Let external waiters steal: give `WaitForAll` (and any external
      help-run path) a pseudo-thief identity so it can drain worker-local
      deques.
- [ ] Track parked-worker count; skip `notify_one` when nobody is parked;
      drop the per-unlock notify in `SpinLock` if it is not load-bearing
      (document why it exists if kept).
- [ ] Expose a cheap `Scheduler::WorkerCount()`; replace the `GetStats()`
      call in `TaskGraph::Execute`.
- [ ] Shard the wait-token mutex (per-slot or hashed shards).
- [ ] Reuse execution scratch in `TaskGraph::Execute` (fixed callbacks on the
      execution state, small-buffer ready lists).
- [ ] Add a PR-fast micro-benchmark (dispatch throughput, steal latency,
      priority inversion probe) so before/after claims carry numbers.

## Tests
- [ ] Contract: under a saturated pool, High-priority dispatches complete
      before queued Low-priority ones (statistical, non-flaky formulation).
- [ ] Contract: `WaitForAll` completes work resident only in worker-local
      deques without sleeping-forever (single-worker forced configuration).
- [ ] Existing `CoreTasks.*`/`CoreTaskGraph.*` suites green; BUG-046
      ordering regression stays green.

## Docs
- [ ] Update `src/core/README.md` scheduler notes (lanes, wake policy,
      waiter stealing).

## Acceptance criteria
- [ ] Priority observable end-to-end from `TaskGraphPassOptions` to worker
      execution order under contention.
- [ ] Benchmark evidence recorded in this file for each landed change
      (or an explicit "no measurable win, dropped" note per item).
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Claiming performance improvements without benchmark evidence
  (see `intrinsicengine-benchmark` policy).
- Unbounded priority levels or per-task dynamic priority.
- Regressing the zero-allocation `LocalTask` dispatch path.

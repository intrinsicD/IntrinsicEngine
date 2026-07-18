---
id: CORE-007
theme: F
depends_on:
  - CORE-005
maturity_target: CPUContracted
---
# CORE-007 — Scheduler priority and worker-wake hardening

## Goal
- Close the remaining efficiency and scheduling gaps in
  `Extrinsic.Core.Tasks`: task priorities that survive into dispatch and
  worker wakeups only when a worker may actually be parked.

## Non-goals
- No graph-level API changes — `CORE-005` (submit) and `CORE-008` (plan
  cache) own those.
- No additional external-waiter stealing work. Retired `CORE-005` already
  shipped the stronger definitive external and worker-owned help scan plus
  the progress-epoch observe/register/recheck handshake.
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
  - Retired `CORE-005` superseded the original waiter-stealing gap with a
    definitive cross-worker help scan and a scheduler-work progress
    handshake. These are prerequisites to preserve, not open scope.
  - Every dispatch does `workSignal.fetch_add + notify_one` and every
    `SpinLock::unlock` does `notify_one` (`Dispatch.cpp`, `Internal.cpp`).
    Only the former is an avoidable worker wake when no worker is parked;
    the latter releases threads parked in the lock's `atomic::wait` slow path.
  - Retired `CORE-005` also removed the old TaskGraph `GetStats()` worker-count
    query by using scheduler-instance identity and worker-eligible-pass state.
  - One global `waitMutex` serializes all parks/unparks/token ops across
    the process (`Core.Tasks.Internal.cppm:103-107`).
  - Parallel submission retains callback wrappers per execution and may grow
    ready-successor scratch during completions. `CORE-008` owns that storage
    together with the retained execution plan.
- ARCH-013 re-review (2026-07-08): Decision unchanged. Scheduler priority,
  stealing, wake, and scratch improvements stay below `JobService` and the
  frame-graph split; priority lanes must remain generic scheduler policy, not
  runtime domain queues.

## Status

- In progress; owner: Codex; branch:
  `codex/core-007-scheduler-hardening-v2`.
- Activated after `CORE-005` retirement on 2026-07-18. Historical prototype
  commit `725af9ce` is evidence only and will not be cherry-picked: it predates
  the scheduler-instance and definitive progress-wait hardening in
  `64b770fc`, so transplanting it would reopen lost-wake and stale-instance
  defects.
- Right-sizing decision: retain fixed priority lanes, TaskGraph priority
  mapping, the single-worker fairness fallback, and conditional worker-wake
  telemetry. External waiter stealing and the hot worker-count query are
  already closed by `CORE-005`; the spinlock notification remains because
  its contended slow path uses `atomic::wait`.
- Wait-token sharding is measure-first optional scope. It lands only if the
  isolated registry-contention probe shows a material bottleneck and the
  candidate improves it without weakening scheduler-instance token
  validation. Per-execution callbacks and ready-list scratch move to
  `CORE-008`, where retained execution state is owned.
- Next verification step: implement priority lanes, the conditional worker
  wake handshake, and evidence-admitted wait-registry sharding; run the exact
  same optimized benchmark for comparison.
- Baseline captured with the harness-only commit `0001f37c` in optimized
  `ci-release`; the measured scheduler/TaskGraph sources are byte-identical to
  `59fbb84a`. Dispatch median was 2.159489 ms for 8,192 tasks
  (3,793,490 tasks/s). The priority probe failed as expected with all 64 Low
  callbacks ahead of High and a first-window error of 16.
- The wait-registry evidence admits sharding into the candidate: one-thread
  acquire/release throughput was 32,005,501 pairs/s, while eight contending
  threads reached only 3,402,629 aggregate pairs/s (1.3289% scaling
  efficiency). Sharding must show a material matched-run improvement without
  regressing scheduler-instance or generation validation.

## Required changes
- [ ] Add a small fixed set of priority lanes to external dispatch (e.g.
      High/Normal/Low inject queues, higher lanes drained first); thread
      `TaskGraphPassOptions::Priority` through worker-eligible pass dispatch.
- [ ] Track parked-worker count; skip `notify_one` when nobody is parked;
      retain and document the separate per-unlock `SpinLock` notification
      because the lock itself parks with `atomic::wait`.
- [ ] Measure the wait-token registry in isolation; either shard it with
      scheduler-instance validation intact and record a material win, or
      explicitly drop the change with the measured result.
- [ ] Add a `ci-release` smoke benchmark (dispatch throughput, priority
      inversion, active-worker wake count, and an isolated wait-registry
      contention probe) so before/after claims carry numbers; keep PR-fast
      coverage deterministic and contract-based.

## Tests
- [ ] Contract: under a saturated pool, High-priority dispatches complete
      before queued Low-priority ones (statistical, non-flaky formulation).
- [ ] Regression: repeated empty-scan-to-park handshakes cannot miss a
      dispatch, and active workers do not emit worker-wake notifications.
- [ ] Regression: a single worker retains owned local progress after its
      fairness probe; TaskGraph priorities reach the scheduler lanes.
- [ ] Existing `CoreTasks.*`/`CoreTaskGraph.*` suites green; BUG-046
      ordering and all `CORE-005` late-enqueue/progress-wait regressions stay
      green.

## Docs
- [ ] Update `src/core/README.md` scheduler notes (lanes, wake policy,
      waiter stealing).

## Acceptance criteria
- [ ] Priority observable end-to-end from `TaskGraphPassOptions` to worker
      execution order under contention.
- [ ] Benchmark evidence recorded in this file for each landed change
      (or an explicit "no measurable win, dropped" note per item).
- [ ] Default CPU gate green.

## Maturity

- Target: `CPUContracted`. This is a CPU-only core scheduler contract; no
  backend-specific Operational follow-up is owed.

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
- Weakening `CORE-005`'s definitive help scan, work-progress publication,
  observe/register/recheck wait protocol, or scheduler-instance validation.

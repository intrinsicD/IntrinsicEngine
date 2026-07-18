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
- Production commits `491aac49` and `54a397e8` add fixed scheduler lanes,
  end-to-end TaskGraph priority, a sequentially consistent conditional-wake
  handshake, candidate telemetry, and 16 wait-token shards. The later commit
  keeps `Normal` dispatch off the lane-count RMW path and rotates shard
  selection thread-locally after a globally dispersed per-instance start.
- Three sequential final captures at production revision `54a397e8` all
  passed. Their middle values were 1.606879 ms / 5,098,081 tasks/s for
  dispatch, zero priority error, 31,267,176 single-thread wait-token pairs/s,
  and 24,688,809 eight-thread aggregate pairs/s (9.8701% efficiency). Against
  the checked-in baseline, contended registry throughput improved 7.2558x
  while the single-thread result changed by -2.3069%. See
  `benchmarks/reports/core_scheduler_hardening_CORE-007.md`.
- Wake evidence is deliberately split: the baseline public stats cannot expose
  notification count, while candidate contracts prove active-worker
  suppression, already-parked notification, and bounded dispatch-vs-park race
  progress. Review-hardening commit `51fa3ea3` also proves all 16 encoded
  shards recycle with generation rejection and dependent TaskGraph successors
  retain priority in worker-local lanes.
- The spin-locked local deque remains. The optimized Release scheduler SLO
  completed all 69,000 expected steals with local-fanout p95 3.347 ms and
  signal-to-resume p99 0.372 ms against 16.667 ms budgets. Queue-contention
  telemetry is nonzero, but there is no matched evidence that a Chase-Lev
  rewrite is required or beneficial.
- Focused verification: all 47
  `CoreTasks.*:CoreTaskGraphCompletionLifetime.*` cases passed; the three
  review-added race/shard/worker-local cases passed 50 consecutive
  repetitions.
- Required gates passed on 2026-07-18 with Clang 23:
  - canonical unsanitized `ci`: `IntrinsicTests` built and 4,090/4,090
    selected CPU tests passed in 51.67 s; the registered ASan-specific GLFW
    leak-control case skipped;
  - fresh grouped `ci-asan`: `IntrinsicCpuTests` built and 2,744/2,744
    selected tests passed serially in 232.84 s, including GLFW leak control;
  - fresh grouped `ci-ubsan`: `IntrinsicCpuTests` built and 2,744/2,744
    selected tests passed serially in 91.70 s; the ASan-specific leak-control
    case skipped;
  - optimized `ci-release`: all three commit-stamped 23-result benchmark
    captures validated strictly, and
    `ArchitectureSLO.TaskSchedulerLocalStealAndWakeCompletionBudgets` passed.

## Required changes
- [x] Add a small fixed set of priority lanes to external dispatch (e.g.
      High/Normal/Low inject queues, higher lanes drained first); thread
      `TaskGraphPassOptions::Priority` through worker-eligible pass dispatch.
- [x] Track parked-worker count; skip `notify_one` when nobody is parked;
      retain and document the separate per-unlock `SpinLock` notification
      because the lock itself parks with `atomic::wait`.
- [x] Measure the wait-token registry in isolation; either shard it with
      scheduler-instance validation intact and record a material win, or
      explicitly drop the change with the measured result.
- [x] Add a `ci-release` matched smoke benchmark for dispatch throughput,
      priority inversion, and isolated wait-registry contention. Record the
      baseline-unavailable wake diagnostic and use deterministic candidate
      contracts for active-worker notification suppression; keep PR-fast
      coverage contract-based.

## Tests
- [x] Contract: under a saturated pool, High-priority dispatches complete
      before queued Low-priority ones (statistical, non-flaky formulation).
- [x] Regression: repeated empty-scan-to-park handshakes cannot miss a
      dispatch, and active workers do not emit worker-wake notifications.
- [x] Regression: a single worker retains owned local progress after its
      fairness probe; initially ready and dependent TaskGraph priorities reach
      external and worker-local scheduler lanes.
- [x] Existing `CoreTasks.*`/`CoreTaskGraph.*` suites green; BUG-046
      ordering and all `CORE-005` late-enqueue/progress-wait regressions stay
      green.

## Docs
- [x] Update `src/core/README.md` scheduler notes (lanes, wake policy,
      waiter stealing).
- [x] Update `docs/architecture/task-graphs.md` with priority, definitive
      completion-help, worker-wake, spinlock-notify, and wait-shard contracts.
- [x] Add the matched benchmark report and report index entry.
- [x] Correct the exact multi-worker processor-reservation totals in
      `tests/README.md`.
- [x] Regenerate the public module inventory; it remains byte-identical at
      386 modules because no module was added or removed.

## Acceptance criteria
- [x] Priority observable end-to-end from `TaskGraphPassOptions` to worker
      execution order under contention.
- [x] Benchmark evidence recorded in this file for each landed change
      (or an explicit "no measurable win, dropped" note per item).
- [x] Default CPU gate green.

## Maturity

- Target: `CPUContracted`. This is a CPU-only core scheduler contract; no
  backend-specific Operational follow-up is owed.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error \
  --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error \
  --timeout 60 --parallel 1
cmake --preset ci-release
cmake --build --preset ci-release \
  --target IntrinsicBenchmarkSmoke IntrinsicBenchmarkTests
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Claiming performance improvements without benchmark evidence
  (see `intrinsicengine-benchmark` policy).
- Unbounded priority levels or per-task dynamic priority.
- Regressing the zero-allocation `LocalTask` dispatch path.
- Weakening `CORE-005`'s definitive help scan, work-progress publication,
  observe/register/recheck wait protocol, or scheduler-instance validation.

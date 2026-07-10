---
id: BUG-067
theme: G
depends_on: []
---
# BUG-067 — JobService completion state lost-update race

## Goal
- Eliminate the lost-update race in `Runtime.JobService` so a completion that
  the main-thread drain has already published/dropped/cancelled can never be
  reverted to the non-terminal `AwaitingGate` state by its worker thread.

## Non-goals
- No change to the snapshot-in/result-out job contract (ARCH-009).
- No change to the `Core::Tasks::Scheduler` worker pool.
- No reworking of the completion-event publish protocol beyond the state-store
  ordering fix.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.JobService.cpp`.
- Introduced by merge `76528e6` ("Merge recovered runtime service extractions"),
  which adopted the extraction-branch `JobService` wholesale over main's parallel
  implementation. Main's version pushed the completion under the lock with a
  different state model and did not exhibit this race.
- The dispatch worker enqueues the completion under `state->Mutex`
  (`Runtime.JobService.cpp:187-193`) and then stores `State = AwaitingGate`
  **outside** the lock (`:194`). The completion is drainable the instant the
  lock is released at `:193`.
- A concurrent main-thread `DrainCompletions` (`:309-341`) dequeues the batch and
  stores the terminal state — `Published` (`:329`), `Dropped` (`:334`), or
  `Cancelled` (`:320`) — lock-free. If the worker is preempted between `:193`
  and `:194`, the worker's `:194` store then overwrites the terminal state back
  to `AwaitingGate`.
- Consequence: the job is stuck non-terminal forever. `ReapCompleted`
  (`:355-378`) never erases it (unbounded `m_Jobs` record leak), `Stats`
  (`:392-405`) reports a phantom in-flight/awaiting-gate job permanently, and
  `IsComplete`/`GetState` never report terminal (any poller waits forever). The
  completion event itself still fires, so the failure is silent. `State` is
  atomic, so this is a logical lost-update, not UB.

## Required changes
- [ ] Set `JobState::AwaitingGate` **before** the `push_back`, or inside the same
      `std::lock_guard` scope that enqueues the completion, so the state is
      already `AwaitingGate` when the record becomes visible to the drain.
- [ ] Audit the remaining `job->State.store(...)` sites in `Runtime.JobService.cpp`
      for the same "publish-then-store" ordering hazard (e.g. the `Running`
      transition at `:164`) and confirm none can be clobbered after becoming
      drainable/observable.

## Tests
- [ ] Add a deterministic contract test that forces the interleaving: enqueue a
      completion, run `DrainCompletions` to publish it, then assert the job's
      final state is terminal and that `ReapCompleted` erases it and `Stats`
      shows zero in-flight. (A test seam or a single-worker scheduler that lets
      the test drain between enqueue and the worker's post-enqueue store.)
- [ ] `ctest --test-dir build/ci --output-on-failure -R RuntimeJobService --timeout 60`.
- [ ] Default CPU gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Docs
- [ ] If the `JobService` README documents completion-state transitions, note
      that the terminal state is authoritative once a completion is enqueued.

## Acceptance criteria
- [ ] A worker's post-enqueue state store can no longer overwrite a terminal
      state set by the drain.
- [ ] Jobs drained in the same tick they complete always reach a terminal state
      and are reaped; no phantom in-flight jobs remain in `Stats`.
- [ ] The new regression test fails against the current ordering and passes after
      the fix.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeJobService --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not make completion state stores non-atomic or drop the mutex around the
  completion queue.
- Do not change the job snapshot/result contract.
- Mixing mechanical file moves with semantic refactors.

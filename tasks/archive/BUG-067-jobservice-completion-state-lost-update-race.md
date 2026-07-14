---
id: BUG-067
theme: G
depends_on: []
---
# BUG-067 — JobService completion state lost-update race

## Status
- Completed 2026-07-13 at `CPUContracted` on branch
  `codex/bug-067-completion-race`.
- Commit/PR reference: production ordering fix `ce1f590c`; this local
  regression/retirement commit.
- Deterministic mutation proof, 100-repeat regression, focused JobService
  contracts, aggregate build, default CPU gate, and strict structural checks
  pass.

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
- Before production fix `ce1f590c`, the dispatch worker enqueued the completion
  under `state->Mutex` (`Runtime.JobService.cpp:187-193`) and then stored
  `State = AwaitingGate` **outside** the lock (`:194`). The completion became
  drainable the instant the lock was released at `:193`.
- A concurrent main-thread `DrainCompletions` (`:309-341`) could dequeue the
  batch and store the terminal state — `Published` (`:329`), `Dropped` (`:334`),
  or `Cancelled` (`:320`) — lock-free. If the worker was preempted between
  `:193` and `:194`, the worker's `:194` store then overwrote the terminal state
  back to `AwaitingGate`.
- Consequence: the job is stuck non-terminal forever. `ReapCompleted`
  (`:355-378`) never erases it (unbounded `m_Jobs` record leak), `Stats`
  (`:392-405`) reports a phantom in-flight/awaiting-gate job permanently, and
  `IsComplete`/`GetState` never report terminal (any poller waits forever). The
  completion event itself still fires, so the failure is silent. `State` is
  atomic, so this is a logical lost-update, not UB.

## Required changes
- [x] Set `JobState::AwaitingGate` **before** the `push_back`, or inside the same
      `std::lock_guard` scope that enqueues the completion, so the state is
      already `AwaitingGate` when the record becomes visible to the drain.
- [x] Audit the remaining `job->State.store(...)` sites in `Runtime.JobService.cpp`
      for the same "publish-then-store" ordering hazard (e.g. the `Running`
      transition at `:164`) and confirm none can be clobbered after becoming
      drainable/observable.

## Tests
- [x] Add a deterministic contract test that forces the interleaving: enqueue a
      completion, run `DrainCompletions` to publish it, then assert the job's
      final state is terminal and that `ReapCompleted` erases it and `Stats`
      shows zero in-flight. (A test seam or a single-worker scheduler that lets
      the test drain between enqueue and the worker's post-enqueue store.)
- [x] `ctest --test-dir build/ci --output-on-failure -R RuntimeJobService --timeout 60`.
- [x] Default CPU gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Docs
- [x] If the `JobService` README documents completion-state transitions, note
      that the terminal state is authoritative once a completion is enqueued.

## Acceptance criteria
- [x] A worker's post-enqueue state store can no longer overwrite a terminal
      state set by the drain.
- [x] Jobs drained in the same tick they complete always reach a terminal state
      and are reaped; no phantom in-flight jobs remain in `Stats`.
- [x] The new regression test fails against the current ordering and passes after
      the fix.
- [x] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeJobService --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Local evidence recorded on 2026-07-13:

- Audited every `job->State.store(...)` site. `Running` is stored before work
  starts and before any queue visibility; the worker's early `Cancelled` and
  `Dropped` paths return without enqueueing; `AwaitingGate` is stored under the
  completion-queue mutex before insertion; and only `DrainCompletions` performs
  state writes after insertion. `Cancel` sets only the cancellation flag.
- With the production ordering, all 10 `RuntimeJobService` contract tests pass.
- Mutating only the ordering back to enqueue/hook/store made
  `CompletionQueuePublicationCannotClobberTerminalState` fail in 35 ms: the
  worker was observably `Running` after publication, then overwrote the drain's
  `Published` state with `AwaitingGate`; `IsComplete` was false, stats reported
  one in-flight/awaiting job, and `ReapCompleted()` returned zero.
- After restoring the production ordering, the regression passed 100
  consecutive forced interleavings with no timing sleeps.
- `cmake --build --preset ci --target IntrinsicTests -j2` passed all 1,457
  build edges.
- The exact default CPU gate passed 3,679/3,679 tests in 468.45 seconds with no
  sanitizer findings.

## Completion

- Completed: 2026-07-13. Commit/PR: production fix `ce1f590c`; this local
  regression/retirement commit.
- Maturity: `CPUContracted`. The deterministic contract runs the real
  `JobService`, scheduler worker, completion queue, main-thread drain, stats,
  and reap paths.
- Follow-up: no `Operational` follow-up is owed; this is a backend-neutral CPU
  state-publication invariant, and the real runtime seam is the intended proof
  boundary.

## Forbidden changes
- Do not make completion state stores non-atomic or drop the mutex around the
  completion queue.
- Do not change the job snapshot/result contract.
- Mixing mechanical file moves with semantic refactors.

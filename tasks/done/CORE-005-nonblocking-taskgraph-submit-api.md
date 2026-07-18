---
id: CORE-005
theme: F
depends_on:
  - BUG-055
  - CORE-006
---
# CORE-005 — Non-blocking TaskGraph submission and completion API

## Goal
- Give `Extrinsic.Core.Dag.TaskGraph` a non-blocking execution mode: a
  `Submit()`-style entry point that returns a completion token (the
  graph-local `CounterEvent` already exists), so callers can start a graph,
  do other work (including across frames), and later poll or wait — and make
  the existing blocking wait park or help-run instead of yield-spinning.

## Non-goals
- No scheduler-level priority lanes or queue redesign — owned by `CORE-007`.
- No compiled-plan caching — owned by `CORE-008`.
- No removal of GPU/streaming domain vocabulary — owned by `CORE-006`.
- No conversion of `Runtime.DerivedJobGraph`/`StreamingExecutor` onto
  TaskGraph in this task; this ships the core capability they would need.

## Context
- Owner/layer: `core` (`Core.Dag.TaskGraph`, `Core.Tasks`).
- Today `Execute()` blocks the caller until the whole graph finishes and the
  caller busy-polls: it drains main-thread-only passes, else
  `std::this_thread::yield()` (`src/core/Core.Dag.TaskGraph.cpp:960-979`) —
  burning a core without helping the scheduler (never steals/pops) and
  without parking. There is no submit/poll/join API; `Reset()` asserts while
  `Executing`.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R12.
- Gated on `BUG-055` because completion-state lifetime must be safe before it
  can escape `Execute`'s stack frame as a token.
- Ordered after retired `CORE-006` so the submission API is implemented
  directly on Core's domain-free task/DAG vocabulary.
- ARCH-013 re-review (2026-07-08): Decision unchanged. This remains core
  scheduler/DAG substrate below `JobService`; the submit token may underpin
  runtime jobs or frame-graph work, but it must not grow runtime/module domain
  vocabulary or duplicate command/event semantics.

## Status

- Completed on 2026-07-18 at `CPUContracted`; owner: Codex; implementation branch:
  `codex/core-005-nonblocking-submit`; principal implementation commits:
  `87740902e9cb0b6f3f37c190474d8a4b7ec06288` and
  `72070e4a339868b4f07743c2cc9f89b6b70ab48e`; progress-wait hardening
  commit: `64b770fc1fc58f392342819989268d171db4a416`.
- The late queue-check-to-park blocker is closed by the progress-wait
  hardening. Worker-backed help loops observe an instance-scoped scheduler-work
  epoch, dispatch and retirement publish progress, and registered waiters
  recheck before parking. The final external-help scan waits through short
  worker-deque lock contention so false emptiness cannot strand published
  work; this definitive scan also applies when the submitting owner is itself
  a scheduler worker. Normal worker-loop stealing remains opportunistic.
- Right-sizing verdict: one copyable completion handle and one shared
  execution-state implementation are justified by the escaped lifetime and
  submit-owner pump contract. Existing `Scheduler`/`CounterEvent` seams were
  extended directly; no factory, service, registry, or second scheduling
  framework was added.
- The full CPU gate exposed a stale deterministic-fallback test that treated a
  one-worker scheduler as single-thread execution and wrote an unsynchronized
  vector from callbacks. A no-ccache clean rebuild reproduced it. The test now
  exercises the documented no-scheduler fallback; the production one-worker
  help-run/steal contract remains intact.

## Required changes
- [x] Add a non-blocking submission API returning a completion handle
      (poll `IsReady()`, blocking `Wait()`); heap/shared-own the execution
      state so it legally outlives the submitting scope (builds on the
      BUG-055 restructure).
- [x] Keep main-thread-only passes correct under submission: define and
      document how the owning thread drains the main-thread queue for a
      submitted graph (explicit `PumpMainThreadPasses()` on the handle is
      acceptable).
- [x] Rework the blocking wait: while the graph runs, the calling thread
      help-executes scheduler work (inject queue and stealing) and uses a
      race-free progress wait when idle—completion-count progress in the
      no-scheduler fallback and scheduler-work progress when worker-backed—
      instead of `yield()` spinning.
- [x] Make `Execute()` a thin wrapper over submit+wait so there is one code
      path.
- [x] Preserve `Reset()`-while-executing protection with the new lifetime
      model (fail-closed error, not assert-only).

## Tests
- [x] Contract: submitted graph completes with correct dependency order while
      the submitting thread runs unrelated work before waiting.
- [x] Contract: main-thread-only passes execute on the owning thread via the
      documented pump path for a submitted graph.
- [x] Contract: blocking wait makes progress when all workers are saturated
      (help-run proven with a single-worker-forced configuration).
- [x] Contract: `Reset()` during a live submission fails closed.
- [x] Existing `CoreTaskGraph.*` and `CoreTasks.*` suites stay green.

## Docs
- [x] Update `docs/architecture/task-graphs.md` with the submit/completion
      model and the main-thread pump contract.
- [x] Update `src/core/README.md` module notes.
- [x] Regenerate `docs/api/generated/module_inventory.md` if `.cppm` surfaces
      change.

## Acceptance criteria
- [x] A graph can be submitted, progressed, and completed without the caller
      blocking; completion observable via token.
- [x] The blocking path no longer spins: CPU profile of an idle wait shows
      parked caller, and the caller demonstrably executes stolen tasks under
      saturation.
- [x] Default CPU gate green.

## Verification

- Pre-fix evidence: `IntrinsicTests` built successfully; focused
  TaskGraph/Tasks contracts
  passed 80/80. The corrected no-scheduler fallback passed 100 repetitions,
  and three one-worker help/steal cases passed 100 repetitions each.
- Pre-fix evidence: the default CPU-supported gate passed 4,078/4,078 with one expected GLFW
  capability skip.
- Pre-fix evidence: the canonical ASan gate configured fresh, built 2,153/2,153 actions, and
  passed 2,732/2,732 selected tests serially with no skip or diagnostic.
  The canonical UBSan gate configured fresh, built 2,153/2,153 actions, and
  passed 2,732/2,732 selected tests serially with one expected LSan-only
  capability skip and no UBSan diagnostic.
- Pre-fix evidence: host policy blocked `perf`
  (`kernel.perf_event_paranoid=4`), so no perf
  sample is claimed. A direct `strace -f -e futex` run covered 50 repetitions
  of 200 worker-to-owner handoffs (10,000 total); the owner issued 10,064
  `FUTEX_WAIT` calls, directly demonstrating parking. The saturation/local
  steal contracts supplied the separate caller-help evidence.
- Post-fix causal evidence: removing only post-enqueue progress publication
  made the two late-child regressions exceed an external five-second timeout
  (exit 124); restoring it passed the focused progress set 100 repetitions
  (400 executions). Removing only post-retirement publication made
  `WorkerCompletionWakesOwnerForMainThreadSuccessor` exceed the same timeout;
  restoring it passed 50 repetitions.
- Post-fix focused evidence: the TaskGraph/Tasks/FrameGraph set passed 79/79.
  The new worker-owned nested-wait/peer-child regression passed 100
  repetitions and proved the owning worker executed the blocked peer's child.
  A strict independent concurrency review found no remaining lost-wake,
  lock-order, lifecycle, or API-sizing blocker.
- Post-fix parking evidence: a direct `strace -f -e futex` run of 200
  worker-to-owner handoffs passed and recorded 353 main-thread waits on the
  scheduler progress futex address (467 main-thread futex waits total).
- Exact committed-state gates: canonical `IntrinsicTests` built 1,160/1,160
  actions and the CPU selector passed 4,082/4,082 with one expected GLFW
  LeakSanitizer capability skip. Fresh `ci-asan` and `ci-ubsan`
  `IntrinsicCpuTests` builds each completed 1,118/1,118 actions; each selector
  passed 2,736/2,736 serially. ASan ran the GLFW LeakSanitizer case, UBSan
  reported its expected LSan-only capability skip, and raw-log scans found no
  sanitizer diagnostic.
- Strict task policy/state-link, layering, test-layout, documentation-link,
  root-hygiene, skill-sync, docs-sync, and clean-workshop checks passed.
  Regenerating the 386-module inventory was byte-identical.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Introducing domain-specific (graphics/streaming) knowledge into core.
- Silent behavior change of existing `Execute()` callers.
- Timing-based synchronization.

## Maturity
- Reached: `CPUContracted`. The submit/poll/pump/wait contract, owner affinity,
  scheduler-instance failure behavior, saturated-worker help path, and both
  queue-check-to-park publication edges are covered on the CPU and sanitizer
  gates.
- No `Operational` follow-up is owed. Consumers adopt the capability in their
  own tasks (for example `GRAPHICS-119` and future streaming-graph
  unification).

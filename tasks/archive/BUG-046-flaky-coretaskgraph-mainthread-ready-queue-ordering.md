---
id: BUG-046
theme: G
depends_on: []
---
# BUG-046 — Flaky `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` (timing-dependent ready-queue batching)

## Goal
- Make `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` deterministic so it cannot intermittently fail in the default CPU correctness gate, by removing its dependence on a fixed wall-clock `WorkerBlocker` sleep to batch the main-thread ready queue.

## Non-goals
- No change to the production `TaskGraph` / `Tasks::Scheduler` priority/cost scheduling semantics — the policy under test (High priority before Low; higher estimated cost first within a priority) is correct and stays.
- No broad rewrite of `tests/unit/core/Test.Core.TaskGraphLegacy.cpp`; scope is this timing-fragile case plus any sibling case sharing the same sleep-based batching assumption.
- `flaky-quarantine` is not the endpoint — quarantine is only an optional stopgap if the flake recurs before the deterministic fix lands, and per `tests/README.md` it must carry this task ID, a reason, and a removal condition.

## Context
- Symptom: `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` (`tests/unit/core/Test.Core.TaskGraphLegacy.cpp:541`) failed once during a full default CPU gate run (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`, 2026-06-18) and then passed 5/5 on isolated re-runs. Observed incidentally during GRAPHICS-091 verification; the failing test is `core`-layer and unrelated to that graphics change.
- Expected behavior: the three `MainThreadOnly` passes drain in deterministic priority+cost order `["HighHeavyMain", "HighMain", "LowMain"]` (assertions at lines 611-613) on every run regardless of host load.
- Impact: a non-deterministic red in the canonical CPU gate. Low severity (passes in isolation) but it erodes gate trust and can mask a real regression behind "probably just the flaky one".
- Root-cause hypothesis: the test occupies all 4 scheduler worker threads with a single `WorkerBlocker` pass that `std::this_thread::sleep_for(40ms)` (line 559). The three `MainThreadOnly` passes each `DependsOn(0u)` the blocker, so they are expected to become ready together and be popped from the main-thread ready queue as one priority/cost-ordered batch. Under full-suite CPU contention the 40ms window is not robust: if the main thread observes readiness at staggered moments (or a worker frees early), the passes can dispatch in arrival order rather than the batched priority/cost order, so `order` diverges from the expected sequence.
- Confirmed root cause: `TaskGraph::Execute()` enqueued each newly-ready successor separately from `onTaskFinished()`. When one worker completion made multiple `MainThreadOnly` successors ready, the executor thread could pop the first queued successor before the rest of the simultaneously-ready batch was present, bypassing the priority/cost queue ordering the test intended to cover.
- Affected symbols: `Extrinsic::Core::Tasks::TaskGraph` (main-thread ready-queue drain), `Extrinsic::Core::Tasks::Scheduler`, and the `WorkerBlocker` timing assumption in the test.
- Owning layer: `core` (task graph / scheduler) and its `unit;core` test.

## Completion
- Completed: 2026-06-24. Commit/PR: pending local commit.
- `TaskGraph::Execute()` now collects newly-ready successors into a batch, publishes all main-thread-ready entries under one queue lock, and then dispatches worker-capable entries. The existing priority queue still owns High before Low and higher estimated cost before lower cost within a priority.
- The regression test no longer uses `std::this_thread::sleep_for(40ms)`; it depends on the production batch enqueue behavior and preserves the existing `["HighHeavyMain", "HighMain", "LowMain"]` assertions.

## Required changes
- [x] Remove the fixed `40ms` `WorkerBlocker` sleep so the regression no longer relies on wall-clock timing.
- [x] Confirm the production main-thread ready-queue drain only orders entries that are present in the queue, then fix the real defect by enqueueing a simultaneously-ready successor batch atomically.
- [x] Sweep `Test.Core.TaskGraphLegacy.cpp` for sibling cases relying on the same sleep-based batching; no other `sleep_for` batching cases remain.

## Tests
- [x] Make `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` deterministic and prove stability under load with a `--repeat until-fail:N` run and a full parallel gate.
- [x] Preserve the existing priority/cost ordering assertions (`order == [HighHeavyMain, HighMain, LowMain]`).

## Docs
- [x] No `flaky-quarantine` stopgap was applied; no test label/docs update was required.

## Acceptance criteria
- [x] The test passes deterministically under repeated and parallel execution (no timing dependence), proven by a `--repeat until-fail:N` run cited in Verification.
- [x] Production scheduling semantics are unchanged except for the genuine drain-ordering defect: simultaneously-ready main-thread successors are now published as one queue batch.
- [x] Default CPU gate is green across repeated runs and no `flaky-quarantine` label remains.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCoreTests
ctest --test-dir build/ci --output-on-failure -R 'CoreTaskGraph\.MainThreadReadyQueueUsesPriorityAndCostOrdering' --repeat until-fail:50 --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'CoreTaskGraph' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j"$(nproc)"
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Weakening or deleting the priority/cost ordering assertions to "fix" the flake.
- Leaving a `flaky-quarantine` label without a linked task ID, reason, and removal condition.
- Changing `TaskGraph` / `Scheduler` scheduling semantics to accommodate the test.

## Maturity
- Target: `CPUContracted`.
- No `Operational` follow-up is owed: this is a core CPU scheduler contract, and the default CPU-supported gate covers the affected behavior.

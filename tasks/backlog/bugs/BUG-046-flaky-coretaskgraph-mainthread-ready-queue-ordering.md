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
- Affected symbols: `Extrinsic::Core::Tasks::TaskGraph` (main-thread ready-queue drain), `Extrinsic::Core::Tasks::Scheduler`, and the `WorkerBlocker` timing assumption in the test.
- Owning layer: `core` (task graph / scheduler) and its `unit;core` test.

## Required changes
- [ ] Replace the fixed `40ms` `WorkerBlocker` sleep with a deterministic gate (e.g. a latch / `std::promise` the blocker waits on, released only after all three `MainThreadOnly` passes are confirmed enqueued/ready) so the ordered-batch precondition holds without relying on wall-clock timing.
- [ ] Confirm the production main-thread ready-queue drain actually orders a simultaneously-ready batch by priority then estimated cost. If the drain pops-as-ready instead of ordering the batch, treat that as the real (non-test) defect and fix the drain, with coverage, rather than papering over it in the test.
- [ ] Sweep `Test.Core.TaskGraphLegacy.cpp` for sibling cases relying on the same sleep-based batching and apply the same deterministic gating.

## Tests
- [ ] Make `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` deterministic and prove stability under load with a `--repeat until-fail:N` run and a full parallel gate.
- [ ] Preserve the existing priority/cost ordering assertions (`order == [HighHeavyMain, HighMain, LowMain]`).

## Docs
- [ ] If a `flaky-quarantine` stopgap is applied before the fix lands, record this task ID, the reason, and the removal condition per `tests/README.md`, and remove the label when the deterministic fix lands.

## Acceptance criteria
- [ ] The test passes deterministically under repeated and parallel execution (no timing dependence), proven by a `--repeat until-fail:N` run cited in Verification.
- [ ] Production scheduling semantics are unchanged unless a genuine drain-ordering defect is found, in which case it is fixed with coverage.
- [ ] Default CPU gate is green across repeated runs and no `flaky-quarantine` label remains.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoreTaskGraph\.MainThreadReadyQueueUsesPriorityAndCostOrdering' --repeat until-fail:50 --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j"$(nproc)"
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Weakening or deleting the priority/cost ordering assertions to "fix" the flake.
- Leaving a `flaky-quarantine` label without a linked task ID, reason, and removal condition.
- Changing `TaskGraph` / `Scheduler` scheduling semantics to accommodate the test.

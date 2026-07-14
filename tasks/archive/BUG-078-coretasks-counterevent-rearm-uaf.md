---
id: BUG-078
theme: G
depends_on: []
---
# BUG-078 — CoreTasks CounterEvent rearm can race coroutine destruction

## Status
- Completed 2026-07-10 at `CPUContracted` on branch `main` in this workspace.
- Commit/PR reference: this local fix commit.
- Focused `CoreTasks` build, deterministic unit regression, repeat gates,
  default CPU gate, and CI-007 clean no-ccache comparison pass.

## Goal
- Eliminate the intermittent `Scheduler::Reschedule` coroutine-handle
  use-after-free observed when `CounterEvent` wait continuations are rearmed.

## Non-goals
- No change to runtime or graphics scheduling policy.
- No broad redesign of `Core::Tasks::Scheduler` beyond the ownership/lifetime
  fix needed for this race.
- No reclamation policy for continuations abandoned when a wait token is
  released or the scheduler shuts down; `BUG-079` owns that adjacent lifetime
  gap.
- No weakening or quarantining of `CoreTasks.CounterEventCanBeRearmedAfterReady`
  to hide the sanitizer failure.

## Context
- Owner/layer: `core`; affected files are `src/core/Core.Tasks.Dispatch.cpp`,
  `src/core/Core.Tasks.WaitToken.cpp`, `src/core/Core.Tasks.CounterEvent.cpp`,
  and `tests/unit/core/Test.CoreTasks.cpp`.
- Observed on 2026-07-10 while running CI-007's clean no-ccache full CPU gate:
  `ctest --test-dir build/ci-no-ccache --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)`.
- The full gate failed 1/3,613 with AddressSanitizer reporting
  `heap-use-after-free` in `std::coroutine_handle<void>::done()` from
  `Scheduler::Reschedule(...)` (`src/core/Core.Tasks.Dispatch.cpp:38`).
- The freed coroutine frame was
  `WaitForCounterTrackStartAndIncrement(...)` from
  `tests/unit/core/Test.CoreTasks.cpp:18`, destroyed by another worker in the
  same `Scheduler::Reschedule` lambda at `Core.Tasks.Dispatch.cpp:39`.
- A direct isolated rerun passed once:
  `ctest --test-dir build/ci-no-ccache --output-on-failure -R '^CoreTasks\.CounterEventCanBeRearmedAfterReady$' --timeout 60`.
  That makes this a timing-sensitive lifetime race, not a deterministic build
  or ccache artifact mismatch.
- The failure resembles the closed `BUG-055` family but occurs on the
  rearm/wait-continuation path rather than the previously fixed TaskGraph
  completion latch path. `CORE-007` may absorb the scheduler hardening work if
  selected, but the observed sanitizer red gate is tracked here as a bug.

## Required changes
- [x] Audit `Scheduler::Reschedule(...)` and wait-token continuation ownership
      so a coroutine handle cannot be inspected (`done()`) after another worker
      destroys the same frame.
- [x] Ensure rearmed `CounterEvent` continuations have single-owner destroy
      semantics across worker threads.
- [x] Preserve the fail-closed `alive` guard semantics without using it as a
      substitute for coroutine-frame ownership.
- [x] Add deterministic instrumentation or a repeatable stress harness that
      widens the rearm race enough to fail before the fix.

## Tests
- [x] Add deterministic unit-level regression coverage that forces the
      captured rearm race interleaving under ASan.
- [x] `ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.CounterEventCanBeRearmedAfterReady$' --repeat until-fail:100 --timeout 60`.
- [x] Default CPU gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Docs
- [x] Update `src/core/README.md` or task documentation if the scheduler
      ownership contract changes.

## Acceptance criteria
- [x] The rearm test fails against the current race or is backed by the
      captured ASan stack, then passes repeatedly after the fix.
- [x] No worker can call `coroutine_handle::done()` or `destroy()` on a frame
      that another worker already destroyed.
- [x] The default CPU-supported gate passes without sanitizer findings.
- [x] The fix preserves core-layer dependency boundaries.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.CounterEventCanBeRearmedAfterReady$' --repeat until-fail:100 --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Local evidence recorded on 2026-07-10:

- Pre-fix focused loop did not reproduce in 200 iterations; the failing
  evidence remains the captured ASan stack from CI-007's clean no-ccache full
  CPU gate.
- `cmake --build --preset ci --target IntrinsicCoreWrapperUnitTests` passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreTasks\.(CounterEventResumeBeforeAwaitSuspendReturnsDoesNotTouchDestroyedFrame|CounterEventCanBeRearmedAfterReady)$' --timeout 60` passed.
- `ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.' --timeout 60` passed 17/17 tests.
- `ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.CounterEventCanBeRearmedAfterReady$' --repeat until-fail:200 --timeout 60` passed.
- `ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.CounterEventResumeBeforeAwaitSuspendReturnsDoesNotTouchDestroyedFrame$' --repeat until-fail:100 --timeout 60` passed.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)` passed 3,655/3,655 tests in 59.64 s.
- `cmake --build build/ci-no-ccache --target IntrinsicTests` passed.
- `ctest --test-dir build/ci-no-ccache --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)` passed 3,614/3,614 tests in 60.44 s.
- After review tightened the regression to wait for explicit frame destruction
  before the original `await_suspend()` returns,
  `ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.CounterEventResumeBeforeAwaitSuspendReturnsDoesNotTouchDestroyedFrame$' --repeat until-fail:100 --timeout 60`
  passed 100 repetitions.
- `python3 tools/repo/check_layering.py --root src --strict` and
  `python3 tools/agents/check_task_policy.py --root . --strict` passed.

## Completion

- Completed: 2026-07-10. Commit/PR: this local fix commit.
- Maturity: `CPUContracted`.
- Follow-up: `BUG-079` owns exactly-once reclamation for parked continuations
  abandoned by wait-token release or scheduler shutdown.

## Forbidden changes
- Do not quarantine or relabel the failing test to make CI green.
- Do not add sleeps as the production fix.
- Do not introduce runtime/graphics dependencies into `core`.

---
id: BUG-113
theme: G
depends_on: []
---
# BUG-113 — Runtime-world reload contract assumes one-frame asset completion

## Goal
- Make the active-world scene-borrower regression drive asynchronous asset
  completion explicitly instead of assuming a scheduler worker reaches
  `Ready` before the next engine frame.

## Non-goals
- No retry-until-green behavior, quarantine label, timeout increase, or CTest
  exclusion.
- No change to production asset, scheduler, world-maintenance, or scene-borrower
  semantics unless the focused diagnosis disproves the harness-timing defect.
- No grouping of `IntrinsicRuntimeContractTests`; the failing case remains an
  individually launched CTest process.

## Context
- Status: completed on 2026-07-18 at `CPUContracted`; owner: Codex; branch:
  `main`; commit: `02945683`; hosted closure run `29625346673`.
- Owner: `tests/contract/runtime/Test.RuntimeWorldRegistry.cpp`.
- Symptom: CI-008 hosted run
  [`29622055604`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29622055604)
  passed registration, all five `--parallel 1` matched pairs, and the first two
  `--parallel 2` pairs. In grouped-plan pair 3, unchanged physical test
  `RuntimeWorldRegistry.EngineRebindsSceneBorrowersBeforeRetiringPreviousWorld`
  then failed only `ReloadAfterDestroySucceeded` at line 494. Artifact
  `8423426394`
  (`sha256:cf5c389566471ef35ff658f92df00bffd9e0e0ac405067ddbc3eb6bcec84ad93`)
  retains the raw log and JUnit.
- Expected behavior: the regression deterministically proves that the model
  handoff can consume reload events both before and after the previous world is
  retired, independent of host scheduling latency.
- Impact: one contended asynchronous completion can fail a required CPU gate
  or abort claim-grade CI timing evidence despite unchanged product behavior.
- `AssetService::Reload()` transitions the asset from `Ready` through
  `QueuedIO` and dispatches `OnCpuDecoded()` when the scheduler is initialized.
  The test calls a second reload on the next variable tick without waiting for
  or driving that completion. `AssetService::CompleteCpuLoadAndFlushEvent()`
  is the existing production-used seam for explicit CPU completion and
  per-asset event delivery.
- Ranked, falsifiable hypotheses:
  1. The test assumes the first reload completes between adjacent fast frames.
     Under contention, the second call observes `QueuedIO` and returns
     `InvalidState`; explicitly completing each prerequisite load/reload will
     remove the failure without changing frame or production behavior.
  2. Two CTest processes collide through shared asset paths or files. If true,
     concurrent same-path cases will be required to reproduce; the current
     virtual loader performs no file I/O and each process owns its service.
  3. Grouped registration changes the failing case. If true, its command or
     result identity will differ between plans; registration evidence shows it
     remains one unchanged physical `IntrinsicRuntimeContractTests` case.
  4. World retirement still corrupts the scene borrower. If true, explicit
     completion will retain a reload/rebind failure or expose sanitizer
     diagnostics rather than fixing only the queued-state race.

## Required changes
- [x] Preserve the real asynchronous load/reload path but explicitly complete
      each prerequisite CPU load through
      `AssetService::CompleteCpuLoadAndFlushEvent()` before the test depends on
      its `Ready` state.
- [x] Retain separate assertions for submission and completion so a loader,
      pipeline, event, rebind, or world-retirement failure remains actionable.
- [x] Keep the fix test-local unless evidence identifies a production defect.

## Tests
- [x] Reproduce or materially amplify the old failure with repeated focused
      execution under CPU contention.
- [x] Run the corrected focused case repeatedly under the same contention and
      without retries inside the test.
- [x] Run the owning `IntrinsicRuntimeContractTests` focused cohort and the
      CI-008 matched evidence lane that exposed the failure.

## Docs
- [x] Update the active bug index and CI-008 evidence narrative.
- [x] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] The contract no longer depends on one-frame worker latency.
- [x] It still exercises real model-scene reload events before and after the
      previous world is retired.
- [x] Hosted CI-008 timing evidence completes without retries, skipped cases,
      label changes, or relaxed assertions.

## Evidence
- With the current one-worker fixture pinned to one CPU beside one CPU burner,
  the old test failed on iteration 7 at
  `ReloadAfterDestroySucceeded`. The fixed binary passed 100/100 under the
  identical contention command, and all eight `RuntimeWorldRegistry.*`
  contracts passed.
- Hosted run
  [`29625346673`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29625346673)
  executed the fixed case in all 15 full-CPU samples at CTest budgets 1, 2,
  and 4. Every sample and exact grouped/individual parity report passed without
  a retry or exclusion.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^RuntimeWorldRegistry\.EngineRebindsSceneBorrowersBeforeRetiringPreviousWorld$' --repeat until-fail:100 --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Sleeping for a guessed duration or retrying reload until it happens to pass.
- Serializing the full CPU gate or excluding this test to hide the defect.
- Weakening the scene-borrower, world-destruction, reload, or entity-count
  assertions.

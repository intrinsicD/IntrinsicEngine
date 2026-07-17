---
id: CI-011
theme: H
depends_on:
  - CI-003
  - CI-005
  - CI-006
  - CI-010
  - BUG-112
---
# CI-011 — Calibrate the slow-test cohort and retain fast sentinels

## Status
- Completed on 2026-07-17 at `Operational`; owner: Codex; branch: `main`.
- Implementation commits: `0e5dc29b`, `9830559c`, `07c6d138`, and
  `ad264dca`; final evidence commit: `23108975`.
- Five-sample hosted timing, exact cohort parity, scheduled ordinary-slow
  execution, and identical-product zero-loss coverage evidence all passed.

## Goal
- Move only repeatedly measured long-running or stress variants out of fast
  gates while retaining cheap deterministic sentinels and complete scheduled
  correctness coverage.

## Non-goals
- No GoogleTest discovery-timeout change; `BUG-091` owns PRE_TEST discovery.
- No grouped execution or CTest/Scheduler worker-budget policy; `CI-008` owns
  those changes after this task establishes the cohort.
- No benchmark-smoke timeout correction; `BUG-088` remains independent.
- No blanket `IClock`, `IExecutor`, `IFileStore`, thread abstraction, or mock
  replacement for tests whose real IO/concurrency is the contract.
- No `slow` label based on one local run or executable size alone.

## Context
- Owner: test executable grouping/labels, timing evidence, and the scheduled
  CPU slow lane. No production layer ownership changes are intended.
- `tests/README.md` defines `slow` for valid tests that regularly exceed one
  second on the reference Linux-Clang runner, boot a full engine/backend, or
  exercise benchmark/SLO behavior.
- Retained local CTest cost data identifies several stress and large-iteration
  candidates, but it is not a current comparable hosted population and cannot
  justify relabeling by itself.
- CTest labels are executable-wide. Marking a mixed executable `slow` can hide
  cheap unit/contract cases, so heavy variants may require a dependency-coherent
  executable split rather than a label on the existing target.
- `CI-010` supplies exact case and source-region parity; `CI-005` supplies the
  final unsanitized fast-preset shape against which latency is measured.

## Required changes
- [x] Collect at least five comparable reference-runner samples for the
      canonical fast and full CPU cohorts, retaining per-case duration,
      executable, labels, skip/fail status, host identity/load diagnostics, and
      selected-case count.
- [x] Classify candidates from median/p95 evidence and declared behavior:
      stress/large iterative/full-engine cases may enter `slow`; cheap analytic
      contracts remain fast even when a sibling variant is heavy.
- [x] Split mixed-duration executables only at natural dependency/runtime
      boundaries so `slow` does not remove cheap sentinels; do not create an
      executable per file.
- [x] For every heavy variant, retain or add a small deterministic sentinel
      covering the same core invariant in PR-fast, and keep the full variant in
      a named scheduled lane.
- [x] Audit measured sleep/thread/file-system hot spots and replace waits only
      where an existing manual pump, latch, scheduler count, fake clock, or
      equivalent public seam preserves the tested contract.
- [x] Update aggregate/selector expectations and prove no reclassified test
      disappears from all required or scheduled workflows; add a label-derived
      `IntrinsicCpuSlowTests` aggregate for the complete scheduled CPU slow
      lane.
- [x] Record before/after fast test wall time and selected-case counts against
      `CI-003` and `CI-005`; make no speed claim from unmatched populations.

## Tests
- [x] Add a regression that compares pre/post fully expanded GoogleTest
      inventories and fails on missing or duplicate cases.
- [x] Use `CI-010` to compare covered production regions/branches for each
      test-only split or label refactor on an identical product commit.
- [x] Prove the fast selector retains every declared sentinel and excludes the
      measured heavy variants.
- [x] Prove the scheduled slow selector executes every reclassified variant
      exactly once with actionable individual case results.
- [x] Repeat the final fast cohort at least five times and report median/p95,
      failures, skips, and case count.

## Docs
- [x] Update `tests/README.md` with the measured cohort, sentinel/slow split
      rule, scheduled reproduction command, and references to `BUG-091` and
      `BUG-088` for independent timeout defects.
- [x] Update CI policy with the retained fast/slow case counts and comparable
      timing evidence.
- [x] Update process/task indexes and regenerate `tasks/SESSION-BRIEF.md` on
      retirement.

## Acceptance criteria
- [x] Every `slow` classification cites a repeatable timing/behavior reason and
      no cheap case is excluded merely because it shared an executable.
- [x] The fast gate retains deterministic sentinels for every moved stress or
      large-iteration invariant.
- [x] Complete scheduled coverage executes every moved case exactly once, and
      `CI-010` reports no lost covered production region for test-only splits.
- [x] Fast-gate median/p95 and selected-case deltas are reported against named,
      comparable baselines without weakening failures, assertions, or timeouts.
- [x] No speculative production test interface is added.

## Evidence
- Baseline
  [`29600380925`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29600380925)
  at `e3fa9187` and candidate
  [`29600381191`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29600381191)
  at `9830559c` each retained five comparable `ubuntu-24.04` samples.
  Full-fast CPU retained 4,062 selected cases while median/p95 fell from
  `31.016747/31.160935` to `24.830993/24.894463` seconds; PR-fast retained
  3,740 selected cases while median/p95 fell from
  `29.233065/29.557700` to `20.297884/20.880414` seconds. Candidate samples
  recorded 38,965 passes, 45 skips, and no failures or errors across the two
  fast populations.
- Exact cohort parity reported eight measured heavy-case removals and eight
  corresponding fast-sentinel additions, with no other case or label drift.
  The ordinary-slow cohort contained exactly the eight moved cases and passed
  all 40 executions across five samples at `4.051059/4.083242` seconds
  median/p95.
- Scheduled nightly-deep
  [`29603101707`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29603101707)
  at `ad264dca` retained one JUnit execution for each of the eight ordinary-slow
  cases, with no skips, failures, errors, duplicates, or unrelated cases.
- Final serialized source-coverage runs
  [`29613834782`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29613834782)
  and
  [`29613834772`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29613834772)
  shared production/build/compile identities. The declared transition reported
  `baseline_cases=4062`, `candidate_cases=4070`, `moved_cases=8`,
  `added_fast_sentinels=8`, `lost_regions=0`, and `lost_branch_arms=0`.

## Verification
```bash
cmake --preset ci-fast --fresh
cmake --build --preset ci-fast --target IntrinsicPrFastTests
python3 tools/ci/collect_test_timing.py --build-dir build/ci-fast --samples 5 --output build/ci-fast/test-timing
python3 tests/regression/tooling/Test.TestCohortParity.py --build-dir build/ci-fast
ctest --test-dir build/ci-fast --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-fast --target IntrinsicCpuSlowTests
ctest --test-dir build/ci-fast --output-on-failure -L slow -LE 'gpu|vulkan|flaky-quarantine' --no-tests=error --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Moving a failing, flaky, or discovery-broken test to `slow` instead of
  diagnosing its defect.
- Raising timeouts or adding sleeps to make the measured cohort pass.
- Dropping stress variants or replacing them solely with smaller sentinels.
- Introducing a general scheduling/test-double abstraction for one offender.

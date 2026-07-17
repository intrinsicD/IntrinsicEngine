---
id: BUG-112
theme: G
depends_on: []
---
# BUG-112 — Clang source coverage is unstable on two production paths

## Status
- In progress on 2026-07-17; owner: Codex; branch: `main`.
- The two minimal source rewrites and focused CPU contracts pass locally.
- Next verification: apply the same production fix to both `CI-011` evidence
  branches and require one serialized hosted A/B pair to normalize and compare
  with zero lost production regions or branch arms.

## Goal
- Make claim-grade identical-product source-coverage A/B complete
  deterministically under Clang 20 without teaching the coverage comparator to
  ignore untrustworthy production counters.

## Non-goals
- No source-coverage schema change, anomaly allowlist, or relaxed counter
  validation.
- No scheduler interface, renderer interface, dependency, or ownership change.
- No test-label, cohort, timeout, or worker-parallelism change.
- No broader scheduler wait/wake hardening; `CORE-007` owns that work.

## Context
- Symptom: atomic hosted `ci-source-coverage` runs `29609841968` (baseline)
  and `29609841892` (candidate) completed every producer, then both failed
  normalization on exported `INT64_MAX` counts. The prior normalized pair also
  had one scheduling-dependent baseline-only branch in
  `Scheduler::WaitForAll`, so it could not prove no-loss parity.
- Expected behavior: the strict validator accepts bounded Clang 20 output from
  both branches, and the declared `CI-011` cohort transition loses no covered
  production region or branch arm.
- Impact: `CI-011` cannot retire, and weakening the validator would let
  compiler-derived or scheduling-dependent counts mask real coverage loss.
- Core ownership: `Scheduler::WaitForAll` performs an unnecessary second
  `inFlightTasks` load after a failed pop. A task may finish between the two
  loads, making the fallback branch inherently schedule-dependent.
- Graphics ownership: Clang 20 maps two uses of `hasNextLevel` in one
  designated initializer inconsistently. The second derived subtraction
  underflows, becomes an unsigned count, and is clamped to `INT64_MAX` by the
  JSON exporter even though the bounded raw profile vector is valid.

## Required changes
- [ ] Retain the first positive `inFlightTasks` observation through
      `atomic::wait` so `WaitForAll` has no redundant racy fallback branch.
- [ ] Compute the recursive-scan block-sums role and offset through one
      `hasNextLevel` control-flow site before constructing the dispatch
      descriptor.
- [ ] Keep the strict saturated/negative coverage-counter rejection unchanged;
      do not add a compiler-coordinate exception or a new report schema.
- [ ] Apply both production rewrites identically to the baseline and candidate
      evidence branches before collecting the final A/B pair.

## Tests
- [ ] Build `IntrinsicCoreWrapperUnitTests` and pass all `CoreTasks.*` cases.
- [ ] Extend the existing large recursive-scan plan contract to assert both
      intermediate and terminal block-sums role/offset pairs, then pass the
      three affected compute-plan cases.
- [ ] Run one serialized hosted `ci-source-coverage` pair and require both
      reports to normalize without saturated counters.
- [ ] Compare the hosted reports with the declared slow-cohort transition and
      require zero lost production regions and branch arms.

## Docs
- [ ] Record the failed-run diagnosis, minimal fix, final hosted run identities,
      and zero-loss result in the CI policy.
- [ ] Update the bug index, retirement log, and generated session brief on
      retirement.

## Acceptance criteria
- [ ] Both diagnosed production paths emit trustworthy Clang 20 coverage
      without suppressing or reclassifying counters.
- [ ] Scheduler and recursive-scan behavior remains covered by focused CPU
      contracts.
- [ ] The final identical-product A/B comparison reports zero lost production
      regions and branch arms.
- [ ] No public surface, dependency edge, test-only production hook, or
      coverage exception is introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreWrapperUnitTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^ComputeParallelPrimitives\.(MultiBlockPrefixScanPlanPinsRecursiveScratch|LargePrefixScanPlanAddsOffsetsFromTopDown|StreamCompactionPlanPinsOffsetsAndScatter)$' --no-tests=error --timeout 60
python3 tools/ci/compare_source_coverage.py --baseline <baseline>/coverage.json --candidate <candidate>/coverage.json --test-cohort-transition tools/ci/slow_test_cohort.json
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Treating `INT64_MAX` as covered, uncovered, or an approved coordinate.
- Adding a private scheduler hook solely to force one coverage branch.
- Expanding either source rewrite beyond the diagnosed behavior-preserving
  control flow.
- Running extra broad gates instead of the focused contracts and one final
  hosted evidence pair.

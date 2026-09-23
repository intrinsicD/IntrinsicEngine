---
id: BUG-216
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive verification incident; the retained timing summary with the isolated reruns is the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this diagnoses a verification-environment timeout, with no geometry algorithm, test, timeout or reusable contract change.
---
# BUG-216 — Curvature refinement test timed out while builds ran concurrently

## Completion — 2026-09-23
Resolved as environmental host contention. The unchanged case passed on its own,
and so did the full CPU selector, after the competing builds finished. No
timeout, label, assertion, registration or geometry source changed.
PR/commit: [PR #1045](https://github.com/intrinsicD/IntrinsicEngine/pull/1045);
tested source `909b422660f3c96c82b75581f46676a8f9195d34`; the enclosing
retirement commit is docs only.

## Goal
Decide whether the RORG-135 full CPU timeout of
`CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve` was host load
or a regression. Do not change the test, its assertions, its label or its timeout.

## Evidence
- Initial failure, full `ci` CPU selector at `909b42266`: 5,012 tests, 1
  failure and 1 skip. The case hit CTest `Timeout` at 30.012 s while the
  `ci-vulkan` and sanitizer builds were compiling. Same-day baseline
  `9831bc4fc`: 17.91 s. The run's other tests with a baseline of at least 1 s
  slowed down 1.14–3.55× (median 2.59×).
- The 30 s limit is the default per-case `TIMEOUT` that `intrinsic_test_executable`
  passes to `gtest_discover_tests`. It overrides the gate's `--timeout 60`.
- RORG-135 changes nothing in the test binary's link closure (`ExtrinsicCore`,
  `IntrinsicGeometry`, `TestSupportObjs`), in `src/geometry`, `src/core`, `cmake/`
  or `tests/unit/geometry`.
- Reruns, same host, source `909b422660f3c96c82b75581f46676a8f9195d34`:
  - build-free focused recheck: 18.12 s
  - full CPU selector recheck: 5,012 tests, 0 failures, 1 expected unsanitized LSan skip, 47.98 s; the timestamps show the ASan/UBSan suites still running
  - isolated case after all builds and suites finished: passed in 18.29 s
- See the [timing summary](../evidence/BUG-216/full-cpu-timing-summary.txt).
  [BUG-190](BUG-190-curvature-refinement-timeout-during-compilation.md) and
  [BUG-207](BUG-207-loaded-ubsan-curvature-timeout.md) record the same
  load-dependent pattern.
- Conclusion: the evidence supports host contention from concurrent compilation
  as the cause of the one missed deadline. It does not indicate a geometry
  defect, and it gives no general timing guarantee.

## Acceptance criteria
- [x] After all builds and test suites finish, run the unchanged registered case alone on the same `ci` build and record its time: passed in 18.29 s.
- [x] Run the complete unchanged CPU selector without competing builds and record totals. Resolve as environmental only if both pass: 5,012 tests, 0 failures, 1 expected skip in 47.98 s. The fully quiet isolated case above is the deciding rerun.
- [x] Record the result here and in RORG-135 without changing any timeout, label, assertion or registration.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve$' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

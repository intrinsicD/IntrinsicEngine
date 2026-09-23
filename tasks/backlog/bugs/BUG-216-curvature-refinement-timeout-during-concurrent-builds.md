---
id: BUG-216
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive verification incident; the retained timing summary and the pending isolated rerun are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this diagnoses a verification-environment timeout, with no geometry algorithm, test, timeout or reusable contract change.
---
# BUG-216 — Curvature refinement test timed out while builds ran concurrently

## Goal
Decide whether the RORG-135 full CPU timeout of
`CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve` was host load
or a regression. Do not change the test, its assertions, its label or its timeout.

## Evidence
- 2026-09-23, Codex main checkout, combined RORG-135 tree (`909b42266`,
  unsanitized `ci`): the full CPU selector registered 5,012 tests and had 1
  failure and 1 skip. The only failure was this case, terminated at 30.012 s
  with CTest `Timeout`.
- The case's `TIMEOUT 30` is the default per-case timeout that
  `intrinsic_test_executable` passes to `gtest_discover_tests`
  (`_intrinsic_default_test_timeout_seconds` in `tests/CMakeLists.txt`). It
  overrides the command-line `--timeout 60`.
- Baseline `9831bc4fc` on the same host, 19 minutes earlier: the case passed
  in 17.91 s, and the full run passed (0 failures, 1 skip) with a 50 s
  selector wall time against 124 s for the failed run.
- The failed run overlapped the full `ci-vulkan` compile and sanitizer builds.
  Every other test with a baseline time of at least 1 s also slowed down: 12
  tests, ratios 1.14 to 3.55, median 2.59. These include unrelated runtime and
  UI cases and sibling curvature cases. Summed per-test time rose from 191.5 s
  to 471.0 s. A 1.68× slowdown is enough to reach 30 s, which is inside the
  observed range. See the [timing summary](../../evidence/BUG-216/full-cpu-timing-summary.txt).
- Ruled out as a source cause: RORG-135 changes no file under `src/geometry`,
  `src/core`, `cmake/` or `tests/unit/geometry`. The test binary
  `IntrinsicGeometryCurvatureExtremaTests` links only `ExtrinsicCore`,
  `IntrinsicGeometry` and `TestSupportObjs` (`support/Test_SanitizerConfig.cpp`),
  all unchanged. The only `tests/CMakeLists.txt` change removes one runtime
  compile-locality source path.
- Precedent: the same case timed out under concurrent compilation in
  [BUG-190](../../done/BUG-190-curvature-refinement-timeout-during-compilation.md),
  and its UBSan group did in [BUG-207](../../done/BUG-207-loaded-ubsan-curvature-timeout.md).
  Both passed unchanged on quiet reruns.
- Working hypothesis, pending isolated reruns: host load from concurrent
  compilation. The evidence does not show a defect in the geometry algorithm
  or test.

## Acceptance criteria
- [ ] After all builds and test suites finish, run the unchanged registered case alone on the same `ci` binary and record its time.
- [ ] Run the complete unchanged CPU selector without competing builds and record totals. Resolve as environmental only if both pass; otherwise investigate the reproducible failure.
- [ ] Record the result here and in RORG-135 without changing any timeout, label, assertion or registration.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve$' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

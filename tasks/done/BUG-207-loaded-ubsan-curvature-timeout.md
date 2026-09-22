---
id: BUG-207
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive verification incident; retained output and the task record provide evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this is a verification environment and test-budget diagnosis, with no proposed geometry algorithm or binding contract change.
---
# BUG-207 — Diagnose loaded UBSan curvature-group timeout

## Goal
Close the grouped curvature-extrema timeout observed during RUNTIME-270 verification without weakening its gate.

## Evidence
The Clang 23 `ci-ubsan` full CPU selector at source `b414cc7d5` timed out
`IntrinsicGeometryCurvatureExtremaTests.Grouped` after its existing 120-second
case timeout. Concurrent ASan/Vulkan compilation initially saturated the 16-core
host; build concurrency was reduced during the run. This is a load hypothesis,
not a confirmed diagnosis. No UBSan runtime error was printed. The refinement
case alone consumed 74.649 seconds. See the [retained output](../evidence/BUG-207/ubsan-loaded-timeout.log).
RUNTIME-270 does not change the geometry curvature-extrema algorithm or its tests.

## Acceptance criteria
- [x] Re-run the same grouped test without simultaneous compilation.
- [x] Confirm the unchanged full UBSan CPU selector passes, or diagnose a reproducible source/harness defect.
- [x] Retain commands/results and the actual resolution; do not increase the timeout or omit the test.

## Verification
```bash
ctest --test-dir build/ci-ubsan --output-on-failure -R '^IntrinsicGeometryCurvatureExtremaTests.Grouped$' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```

The unchanged group passed without simultaneous compilation in 78.31 seconds
(78.69-second CTest wall time), under the same 120-second case limit. See the
[quiet run](../evidence/BUG-207/ubsan-quiet-pass.log). This supports build
contention as the cause of the observed timeout; the full UBSan selector also passes. No timeout, label, test or algorithm was changed.

## Completion — 2026-09-22
Retired at the CPUContracted verification endpoint. No source, timeout, label or
registration change was necessary. The full unchanged exclusion-only UBSan
selector at `1a578a1aa` passed 3,288 selected tests (3,287 passes and one expected
GLFW/LSan skip), zero failures, 296.05 seconds. The curvature group passed in
81.24 seconds. This agrees with the quiet focused recheck and supports concurrent
build contention as the transient cause; it does not establish a runtime speedup.
See [full-selector summary](../evidence/BUG-207/ubsan-full-pass.log).
PR/commit: enclosing retirement commit; tested source `1a578a1aa`. No deferred work.

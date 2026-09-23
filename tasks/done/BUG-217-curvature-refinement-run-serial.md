---
id: BUG-217
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive CI/test-scheduling fix; retained CI links and local/hosted probe timings are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this changes only the CTest scheduling property of one discovered test case, with no engine, algorithm or reusable contract change.
---
# BUG-217 — Run the curvature refinement case without CPU-bound siblings

## Completion — 2026-09-23
Resolved. With `RUN_SERIAL` on the one discovered case, the one-CPU parallel
probe that previously timed out passes, the full CPU gate passes, and hosted
pr-fast passes. The test body, identity, labels, `TIMEOUT 30`, grouped
registration and production source `909b422` are unchanged.
PR/commit: [PR #1045](https://github.com/intrinsicD/IntrinsicEngine/pull/1045);
fix `4f3ff16831b2721da6b01631748e5784f9c181aa`; the enclosing retirement commit
updates only a comment and task records.

## Goal
Keep `CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve` within its
unchanged 30-second budget in individually discovered, parallel CTest runs.
Set `RUN_SERIAL` on that single case.

## Context
- The hosted pr-fast run for PR #1045 timed out this unchanged case at 30.02 s
  ([run 35841932080](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35841932080)),
  with no concurrent build.
- The same source passed in 23.51 s one run earlier
  ([run 35838290607](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35838290607)).
- pr-fast runs its exact-regex chunks with `-j4` on a fresh tree. CTest starts
  tests in number order, so the single-threaded case overlaps its CPU-bound
  CurvatureExtrema siblings.
- Local probes on the same binary:
  - one CPU, all 10 cases at `--parallel 4`: reproduces the timeout (30.01 s) while the other 9 pass
  - one CPU, case alone: 17.10 s
  - two CPUs, all 10 at `--parallel 4`: passes in 23.36 s
- See the [retained evidence](../evidence/BUG-217/timeout-evidence.txt).
- The retired [BUG-216](BUG-216-curvature-refinement-timeout-during-concurrent-builds.md),
  [BUG-190](BUG-190-curvature-refinement-timeout-during-compilation.md) and
  [BUG-207](BUG-207-loaded-ubsan-curvature-timeout.md) attributed earlier
  misses to concurrent compilation. These probes show that CPU competition from
  sibling tests alone is enough. Those retired records stay unchanged.
- Fix: the existing generated property fixup in `tests/CMakeLists.txt` sets
  `RUN_SERIAL TRUE` on this one discovered case. It fails closed if the built
  binary does not discover the case.
- Unchanged: grouped `NO_DISCOVER` registration, test identity and body,
  assertions, fixtures, labels, `TIMEOUT 30`, workflows and geometry sources.
  `tests/README.md` records the exception in one sentence.
- Cost: other tests in the same CTest invocation wait while this case runs.

## Acceptance criteria
- [x] Test metadata shows the case with `RUN_SERIAL` true and still `TIMEOUT 30`; no other property or test changes. Across `ci`, `ci-asan`, `ci-ubsan` and `ci-vulkan`, `RUN_SERIAL` was added only to this case in the individually discovered `ci`/`ci-vulkan` registrations. Grouped sanitizer registrations are unchanged, and all four compared test binaries are identical.
- [x] One-CPU `--parallel 4` probe of the 10-case CurvatureExtrema set, repeated 3 times before and after the change, shows the timeout removed after it. Before: timeouts at 30.01, 29.99 and 30.01 s. After: all 10 cases passed in each run, with the case at 18.00, 18.69 and 18.03 s.
- [x] Full CPU gate passes on the combined tree: 5,012 tests, 0 failures, 1 expected unsanitized LSan skip, 68.47 s (case 17.96 s).
- [x] Normal hosted pr-fast passes for PR #1045: [run 35844618599](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35844618599) succeeded in 8m28s. The case started after its last sibling finished and passed in 19.03 s; docs-validation also passed.

## Verification
```bash
ctest --test-dir build/ci --show-only=json-v1 -R '^CurvatureExtrema\.RetriangulationAndRefinementRetainCenterCurve$'
taskset -c0 ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema\.' --parallel 4 --repeat until-fail:3
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

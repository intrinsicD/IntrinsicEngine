---
id: BUG-217
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive CI/test-scheduling fix; retained CI links, probe timings and the pending pre/post probes are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this changes only the CTest scheduling property of one discovered test case, with no engine, algorithm or reusable contract change.
---
# BUG-217 — Run the curvature refinement case without CPU-bound siblings

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
- The retired [BUG-216](../done/BUG-216-curvature-refinement-timeout-during-concurrent-builds.md),
  [BUG-190](../done/BUG-190-curvature-refinement-timeout-during-compilation.md) and
  [BUG-207](../done/BUG-207-loaded-ubsan-curvature-timeout.md) attributed earlier
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
- [ ] Test metadata shows the case with `RUN_SERIAL` true and still `TIMEOUT 30`; no other property or test changes.
- [ ] One-CPU `--parallel 4` probe of the 10-case CurvatureExtrema set, repeated 3 times before and after the change, shows the timeout removed after it.
- [ ] Full CPU gate passes on the combined tree.
- [ ] Normal hosted pr-fast passes for PR #1045; if it still fails, diagnose further before closing.

## Verification
```bash
ctest --test-dir build/ci --show-only=json-v1 -R '^CurvatureExtrema\.RetriangulationAndRefinementRetainCenterCurve$'
taskset -c0 ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema\.' --parallel 4 --repeat until-fail:3
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

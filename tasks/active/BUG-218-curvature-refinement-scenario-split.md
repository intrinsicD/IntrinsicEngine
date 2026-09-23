---
id: BUG-218
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive test-structure fix; retained CI counterexample, gdb scenario timings and the pending verification are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this splits one geometry unit test into two independently registered scenarios and moves one CTest scheduling property, with no engine, numeric, tolerance or reusable contract change.
---
# BUG-218 — Split the curvature retriangulation and refinement scenarios

## Goal
Register the two independent scenarios of the former
`CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve` as separate
cases. Keep every grid, parameter and assertion, and keep only the dominant
refinement case serialized.

## Context
- [BUG-217](../done/BUG-217-curvature-refinement-run-serial.md) removed
  CPU-bound sibling overlap. On hosted
  [run 35845852620](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35845852620),
  the combined case nevertheless ran alone and timed out at 30.04 s, after
  19.03 s on the previous runner. BUG-217 was a partial mitigation; its record
  and BUG-216 stay as written.
- Measured scenario cost on the existing Debug binary:
  - `Grid(40, alternate)` Extract: 3.55 s
  - `Grid(60)` Extract: 15.49 s (81% of the case)
  - See the [retained evidence](../evidence/BUG-218/split-evidence.txt).
- Change in `Test.CurvatureExtrema.cpp`:
  - `RetriangulationRetainsCenterCurve`: `Grid(40, false, true)`.
  - `RefinementRetainsCenterCurve`: `Grid(60, false, false)`.
  - Each keeps the former body: `Extract(mesh, Parameters())`,
    `ASSERT_TRUE(r.Succeeded())`, and `CheckCenter` on the PrincipalValley and
    MeanValley midpoints.
  - A retriangulation failure no longer hides the refinement result.
- Scheduling: the existing generated `RUN_SERIAL` registration moves to the
  refinement case only. The ~3.5 s retriangulation case runs like its
  unserialized siblings.
- Unchanged: the 30 s per-case default, labels, the grouped `NO_DISCOVER`
  wrapper, workflows, engine and numeric code.
- Limits: the split takes about 19% off the critical case by construction. It
  does not bound hosted runner speed. If hosted refinement still times out,
  escalate with that evidence; do not retry or split further.

## Acceptance criteria
- [ ] The changed geometry-test target builds in `ci`, `ci-asan`, `ci-ubsan` and `ci-vulkan`.
- [ ] Test metadata delta: the old combined case is replaced by exactly the two new cases. `RUN_SERIAL` is on refinement only, both have `TIMEOUT 30`, labels are unchanged, and grouped and GPU registrations are unchanged.
- [ ] A one-CPU `--parallel 4` run of the 11 CurvatureExtrema cases passes three times in a row.
- [ ] The full CPU, full ASan and full UBSan gates pass.
- [ ] Normal hosted pr-fast passes for PR #1045.

## Verification
```bash
ctest --test-dir build/ci --show-only=json-v1 -R '^CurvatureExtrema\.'
taskset -c0 ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema\.' --parallel 4 --repeat until-fail:3
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```

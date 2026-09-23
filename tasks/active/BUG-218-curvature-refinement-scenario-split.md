---
id: BUG-218
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive test-structure and budget fix; retained CI counterexamples, gdb scenario timings and the pending verification are the evidence.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this splits one geometry unit test into two independently registered scenarios and sets case-scoped CTest scheduling/timeout properties, with no engine, numeric, tolerance or reusable contract change.
---
# BUG-218 — Split the curvature scenarios and budget the refinement case

## Goal
Register the two independent scenarios of the former
`CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve` as separate
cases. Keep every grid, parameter and assertion. Serialize only the dominant
refinement case and give it a case-only 60 s `TIMEOUT`.

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
- Split alone was not enough. On hosted
  [run 35849534652](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/35849534652)
  at `d6d3bccfe`, `RefinementRetainsCenterCurve` ran alone after its siblings
  and timed out at 30.01 s; the other 115 cases passed. Retriangulation took
  8.53 s against 3.39 s locally. Orientation took 17.07 s against 6.93 s.
- Budget: the existing fixup registration sets `TIMEOUT 60` on the refinement
  case only, next to `RUN_SERIAL`. 60 s is the CPU gate's outer `--timeout`.
  - Estimate, not a measurement: local refinement 14.38 s × the 2.5–3.1×
    hosted/local ratio of the longer siblings ≈ 36–44 s. Those siblings shared
    CPUs; refinement ran alone.
  - The only hosted refinement measurement is the > 30.01 s lower bound.
  - A hang still fails at 60 s.
  - This is a diagnosed, case-scoped budget change. It is not a broad timeout
    change, slow label or quarantine.
- Unchanged: the 30 s default for every other discovered case, labels, test
  selection, the grouped `NO_DISCOVER` wrapper, workflows, engine and numeric code.

## Acceptance criteria
- [x] The changed geometry-test target builds in `ci`, `ci-asan`, `ci-ubsan` and `ci-vulkan`.
- [x] Split metadata delta (`895980f87`): the old combined case is replaced by exactly the two new cases. `RUN_SERIAL` is on refinement only, both have `TIMEOUT 30`, labels are unchanged, and grouped and GPU registrations are unchanged.
- [x] Budget metadata delta (`4cdf626f0`): the curvature target builds in all four presets. The only change is `RefinementRetainsCenterCurve` going from `TIMEOUT 30` to `TIMEOUT 60`, keeping `RUN_SERIAL`, in `ci`/`ci-vulkan`. Grouped `ci-asan`/`ci-ubsan` are unchanged, and all four curvature binaries' SHA-256 hashes are identical.
- [x] Three separate one-CPU `--parallel 4` whole-cohort invocations of the 11 CurvatureExtrema cases each pass, 11/11. Refinement took 15.47, 16.15 and 15.90 s. An earlier `--repeat until-fail:3` variant was not green; see the log.
- [x] The full CPU, full ASan and full UBSan gates pass on the split source `895980f87`:
  - CPU: 5,013 tests, 0 failures, 1 expected skip, 60.27 s; refinement 14.38 s
  - ASan: 3,333, 0 failures, 0 skips, 721.31 s; curvature group 99.72 s
  - UBSan: 3,333, 0 failures, 1 expected skip, 318.69 s; curvature group 84.13 s
- [x] The normal local CPU run passes again with the 60 s budget: 5,013 tests, 0 failures, 1 expected skip, 60.09 s (refinement 15.72 s).
- [ ] Normal hosted pr-fast passes for PR #1045.

## Verification
```bash
ctest --test-dir build/ci --show-only=json-v1 -R '^CurvatureExtrema\.'
for i in 1 2 3; do taskset -c0 ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema\.' --parallel 4 || exit 1; done
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```

## Log
- 2026-09-23: The four presets built. The metadata delta is exactly as intended.
- 2026-09-23: The first one-CPU probe used `--repeat until-fail:3` and was **not green**. Both new cases passed, but the unchanged `OrientationReversalExchangesRidgeValley` timed out at 30.02 s. Orientation alone on the same binary takes 7.67 s.
- 2026-09-23: `--repeat` reruns individual tests inside one invocation, so its sibling overlap differs from the earlier before/after probes, which were separate whole-cohort invocations. The probe was therefore corrected to three separate whole-cohort invocations, and all three passed 11/11.
- 2026-09-23: The repeated-test variant shows that unserialized heavy siblings can still approach 30 s under extreme one-CPU sharing. No test or gate was changed for it.
- 2026-09-23: Full CPU, ASan and UBSan passed on `895980f87`.
- 2026-09-23: Hosted run 35849534652 at `d6d3bccfe` timed out the serialized refinement case alone at 30.01 s. The unchanged 30 s budget proved insufficient. The final proposal is the case-only 60 s budget; its metadata, local rerun and hosted result are pending.
- 2026-09-23: Budget metadata and the local full CPU run passed at `4cdf626f0`. Hosted run 35851119431 then failed an unrelated UI cache test ([BUG-219](BUG-219-uv-panel-cache-snapshot-race.md)) before reaching the curvature chunk, so the hosted budget criterion stays open.

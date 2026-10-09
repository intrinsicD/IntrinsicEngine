**REVISE** for supplied commit `0d581c8c9`, digest `b35c917286f67a4ecc9e1ae8e68941da4ec498ff9934734dfc7f7704af46af6e`.

References: [A]( /home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-a.md), [B](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md), [driver/analysis D](/home/alex/Documents/IntrinsicEngine/tools/benchmark/cpd_landmark_variants.py), [analysis test T](/home/alex/Documents/IntrinsicEngine/tests/regression/tooling/Test.CpdLandmarkVariantsAnalysis.py), [runner R](/home/alex/Documents/IntrinsicEngine/benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp), [mechanism tests C](/home/alex/Documents/IntrinsicEngine/tests/unit/geometry/Test.CoherentPointDriftAccelerated.cpp). Numbers below are file lines.

**Prior blocking findings**

1. **Resolved: primary cost metric.** The operator explicitly chose repeated wall time. Biased kernel counts are now explanatory, with their omissions disclosed (B:12–16, 35–39; A:98–100). Fixing that counter is no longer necessary for this decision.

2. **Not resolved: gate enforcement.** The operator-authorized pair-exclusion policy replaces the previous stop policy appropriately. However, B:43 requires both passes of every timed arm to complete. D:221–230 leaves `deterministic=True` when a nonbaseline arm is missing. That pair remains eligible, contrary to the gate.

3. **Not fully resolved: reference/control descriptions.** Auto and floor behavior are clarified (A:46–57; B:86). A:59 still says “only non-positive sums reject,” omitting eigensolver and non-finite-statistic failures. This needs correction to match the implementation’s rejection conditions.

4. **Resolved: positive-control consistency.** Baseline engagement and equivalence now gate each pair (B:45–46; D:231–233), as the operator requested.

5. **Not resolved: resumption/evidence preservation.** Commit/hash/size checks, reference length validation, and locking improve matters (D:111–138, 180). Unjournaled reference/JSON artifacts are still deleted (D:162–166), contradicting “nothing is overwritten” (A:78). Analysis also overwrites its prior artifact (D:297).

6. **Not fully resolved: telemetry/interpretation.** Completed-update semantics and P7 reporting are fixed (A:96–97; D:288). B:81 still asserts that an ineffective choice implies identical engagement counts; equal practical performance does not require that identity.

**New blocking problems**

- **The test endorses the missing-arm gate bug.** T:111–115 expects a failed variant to be ranked worse while retaining its pairs. This conflicts with B:43. Reconcile the specification, implementation, and test; under the present gate, those pairs must be excluded.
- **Missing evidence becomes successful predictions.** With no usable pairs, P3 evaluates true because “no ranking” differs from “beats baseline” (D:282). P4 also evaluates true because its sign set is empty (D:290–296). These require an unevaluated/unknown state, with regression coverage.
- **Analysis bypasses provenance validation.** D:208–210 loads records without the commit/hash/size checks applied during execution. It can rank technical-size or mixed-provenance records. Validate the frozen input set before analysis.
- **CPU pinning silently becomes optional.** D:139 runs unpinned when `taskset` is unavailable, although A:18 makes pinning part of the configuration. Analysis does not reject such records. Refuse this configuration deviation.

**Runner seed change:** no contamination found. Fixture randomness is constructed separately (R:46–65), while R:153 changes landmark sampling. Farthest-point selection uses the start index rather than this seed, preserving the shared G construction. Box streams vary across fixture seeds as documented. This does not independently estimate landmark-seed variability.

**Checklist**

“Plan” below means prospective evidence accepted for a † item, not an observed pass.

| Item | Evidence or gap |
|---|---|
| 1 | A:40–59 fixes shared BCPD settings; variants match the question. |
| 2 | Units/scales stated; initialization term-magnitude comparison missing. |
| 3 | Implementation inspected; adaptive counts, schedules, fallback disclosed. |
| 4 | Full/partial and perturbation/floor conditions separate relevant cases. |
| 5† | Partial plan: geometry/noise specified; per-seed descriptive statistics incomplete. |
| 6 | Raw inspection reported for seed 1 only (A:37). |
| 7† | Dense-comparison mechanism assertions exist (C:675–715); not executed here. |
| 8† | Input-motion and redraw checks exist (C:719–784); no hidden/future channel. |
| 9† | N/a: no teacher/student target. |
| 10 | Truth is separate from registration inputs (R:46–65). |
| 11 | Grid contains requested arms; engagement gates detect baseline nonactivation. |
| 12 | Full-reference truth guard exists; shared partial-reference failure remains unguarded. Absolute correctness is outside the chosen equivalence decision. |
| 13 | N/a: controls do not claim information-removal ablations. |
| 14† | **Fails:** missing-arm gate and empty-data prediction successes. |
| 15 | Fixed world-unit, all-source RMS; localized errors can be averaged away under the chosen metric. |
| 16† | Planned baseline, floor, negative, perturbation controls (B:41–52). |
| 17† | Six paired blocks, two timings, 10%/spread rule specified; uncertainty remains heuristic. |
| 18 | Shared fixture generation preserved; seed change does not shift data streams. |
| 19† | Matched 150-update caps; full-reference accuracy gate; convergence not required. |
| 20 | N/a: no cross-fixture training or tuning stage specified. |
| 21 | Rules frozen, but gate/test mismatch remains. |
| 22 | **Gap:** inspected tests check locations/redraws, not sampling distribution. |
| 23† | Technical tests reported; revised final-size box-seed path unverified. |
| 24 | Timeout and memory evidence reported; pinning fails open. |
| 25 | Baseline duration reported; phase profile supporting speed hypothesis absent. |
| 26 | Serial execution and lock exist; evidence-preservation mismatch remains. |

I read both revised layers fully. No execution, delegation, outside-repository diff access, or digest verification occurred; the analysis can currently rank incomplete evidence and report unsupported prediction successes.
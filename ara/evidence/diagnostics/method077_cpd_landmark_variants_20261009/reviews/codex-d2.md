**REVISE** for supplied digest `2a83e7093709d23acbfb1adff947544244b543d3e242fb771acb72f8a915074d` at commit `8021b37f0`.

References: [A](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-a.md), [B](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md), [driver/analysis D](/home/alex/Documents/IntrinsicEngine/tools/benchmark/cpd_landmark_variants.py), [analysis tests T](/home/alex/Documents/IntrinsicEngine/tests/regression/tooling/Test.CpdLandmarkVariantsAnalysis.py), [sampling tests C](/home/alex/Documents/IntrinsicEngine/tests/unit/geometry/Test.CoherentPointDriftAccelerated.cpp). Numbers below are file lines.

**Previous blockers**

- **Gate/test mismatch — resolved.** B:53–56 now explicitly distinguishes identical failures from asymmetric outcomes. D:241–244 implements that contract; T:113–119 covers asymmetric outcomes.
- **Control descriptions — resolved.** A:59–62 now includes numerical/eigendecomposition rejection.
- **Artifact preservation — resolved.** D:75–82, 175–200 archives existing artifacts, including timeout references; D:322–327 versions analysis outputs.
- **Null/telemetry wording — resolved.** B:95–98 permits differing engagement counts; update-count semantics and P7 remain corrected.
- **Unevaluable predictions — not fully resolved.** P3 and empty-comparison P4 are corrected (D:296–321; T:121–127), but P5 still silently excludes missing variant measurements.
- **Analysis provenance — not fully resolved.** D:224–228 rejects mixed provenance, wrong point counts, and unpinned records. It does not require nonempty commit/binary identities or compare them with the expected frozen identities.
- **Optional pinning — resolved.** D:149–152 refuses execution without `taskset`; analysis rejects unpinned records.
- The previously resolved primary-metric and positive-control findings remain resolved.

**Remaining concrete blockers**

1. **P5 can pass without all nine variants’ measurements.** Under the newly explicit policy, a variant failing both repeats leaves its pair eligible. D:309–310 then omits that variant from the maximum instead of marking P5 unevaluable. The fabricated case already present at T:136–141 would therefore allow P5 to pass using only eight variants. Require complete measurements for this prediction and add an assertion covering that case. This does not require changing the operator’s ranking policy.

2. **Homogeneous wrong or missing provenance still passes.** For example, records all carrying `(None, None, 100000, True)` satisfy D:224–225. Likewise, an entire dataset from another commit/binary passes if internally consistent. T:129–134 tests mixed identities, not uniformly wrong or absent identities. Bind analysis to complete, expected frozen identities.

**Checklist gaps**

| Items | Current status |
|---|---|
| 2 | Configuration rationale improved (B:29–37). I narrow my earlier magnitude concern: Bayesian stopping uses no added coherence term ([solver:592](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.cpp:592)). |
| 5†, 6 | Still limited to documented seed-1 statistics/examples (A:38–39). |
| 12 | Shared partial-reference failure remains a stated-scope limitation; no new absolute-accuracy gate requested. |
| 14†, 21 | Still unresolved because P5 can report success with missing measurements. |
| 22 | Resolved: random/Halton union-region frequency assertions use fresh evaluators (C:973–1024); the task log reports an overlap-correction mutation failure. |
| 23† | Final-size revised box-seed execution is now reported (A:86–87); not independently rerun. |
| 24, 26 | Pinning and preservation fixes resolve the identified implementation gaps. |
| 25 | Baseline duration remains documented; supporting phase-profile evidence remains absent. |

Optional cleanup: A:73 still says references are “deleted”; the task goal still names kernel counts despite wall time now deciding.

No builds, runs, delegation, external-diff access, or digest verification occurred. The remaining risk is accepting incomplete prediction evidence or results unbound to the reviewed packet.
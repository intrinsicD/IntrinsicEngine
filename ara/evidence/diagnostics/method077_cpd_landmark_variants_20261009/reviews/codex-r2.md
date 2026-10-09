**REVISE — blocking findings prevent starting this packet.**

Reviewed against the supplied digest `de8b3cc3d307a814f87c20761271ca79ff8afe64ed7eff241f2f4bafcdd1fa56`; I did not independently verify the digest. The exact numbered-checklist mapping remains incomplete for the access-boundary reason below.

**1. Alignment with the operator’s questions**

The run covers the requested fixed/resampled point sampling and random/structured box sampling within the chosen synthetic, \(10^5\)-point, Bayesian CPD scope. Question 2 concerns the preceding implementation; this experiment addresses questions 1 and 3.

Reusing the existing registration implementation is a reasonable shortest path. I found no root entry containing “goal,” but that does not establish repository-wide absence of a goal file. The claim that this is the editor’s execution path was not independently established.

The main qualification is the endpoint: total registration E-step kernel counts measure computational work across potentially different optimization trajectories. They do not directly establish which variant computes P fastest. Layer B’s assertion that kernel evaluations dominate runtime needs support, particularly because landmark selection and eigendecompositions vary between arms. See [Layer B:16–19, 42–45](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md:16).

**2. Blocking findings and reconstruction mismatches**

1. **The primary cost metric omits work on early rejection.** `NystromRows` can return before assigning `evaluations`; the caller then adds only a nominal target-pass count. Already performed Gram construction and source work disappear from the count. Since rejected attempts can differ systematically between variants, the 10% ranking can be wrong. Fix and verify accounting before using it as the primary endpoint. Evidence: [E-step:1497](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1497), [1556](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1556), [1659–1668](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1659).

2. **The declared stop rules are not executable gates.** The driver checks return codes and selected identity fields, then continues. It never evaluates reference truth error, baseline engagement, negative/floor controls, or repeat determinism. An exact failure prevents that pair’s comparisons but does not stop subsequent pairs. Implement the declared stop behavior and make an unsuccessful plan distinguishable through its exit status. Evidence: [Layer B:68–78](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md:68), [driver:125–147](/home/alex/Documents/IntrinsicEngine/tools/benchmark/cpd_landmark_variants.py:125).

3. **Reference and control descriptions overstate what executes.** “Exact” is tolerance-controlled Auto, potentially including truncated/fast-Gauss evaluation. The negative control starts at two landmarks but can increase that count or fall back. The floor tolerance does not prohibit accepting a numerically zero error. These are valid prospective controls only with accurate descriptions and checked signatures. Likewise, zero Nyström engagement need not produce identical output to Auto because fallback disables fast Gauss. Reconcile [Layer B:63–85](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md:63) with [E-step:1652–1680](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1652).

4. **Positive-control acceptance is inconsistent.** P6 requires baseline engagement and equivalence in every pair. The kill condition checks only zero engagement across *all* pairs and never rejects a nonequivalent baseline. Consequently, variants can be ranked against a baseline that fails its stated positive-control contract. Distinguish predictions from measurement-validity requirements and enforce the latter consistently. Evidence: [Layer B:60–61](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md:60), [71–72](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md:71).

5. **Resumption does not enforce the frozen experiment.** Existing records are skipped without validating their commit, runner hash, or point count against this invocation. Result matching omits points and threads. Thus technical-test records or records from another binary can populate the same nominal grid. All recorded failures/unevaluated cells also remain skipped; unjournaled JSON/reference artifacts can be deleted. Bind resumption to the frozen configuration and define interruption/retry handling that preserves raw evidence. Evidence: [driver:61–67](/home/alex/Documents/IntrinsicEngine/tools/benchmark/cpd_landmark_variants.py:61), [88–121](/home/alex/Documents/IntrinsicEngine/tools/benchmark/cpd_landmark_variants.py:88).

6. **Telemetry and analysis descriptions need reconciliation.** `nystrom_iterations` counts completed updates, potentially fewer than 150; it excludes a terminal convergence-check E-step. Equal usefulness does not imply equal engagement counts, as the stated ineffective-method signature suggests. The analysis also promises P1–P6 while omitting floor-control prediction P7. Correct these before freezing the interpretation. Evidence: [Layer B:32, 63–66, 108–110](/home/alex/Documents/IntrinsicEngine/ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md:32), [solver:832–881](/home/alex/Documents/IntrinsicEngine/src/geometry/Geometry.Registration.CoherentPointDrift.cpp:832).

**3. Checklist evidence**

I cannot truthfully assign findings to exact items **1–26**: their wording is not in the supplied instructions or permitted packet, and the installed skill is outside the allowed repository. No matching copy appeared in the repository’s three skill directories. I requested the wording; it has not arrived. This is **unassessed**, not “n/a” or a pass.

The available evidence supports these domain assessments:

| Domain | Assessment |
|---|---|
| Question ↔ configuration | Requested variants and chosen scale/model align; speed interpretation requires qualification. |
| Mechanism | Real library registration executes; adaptive counts, fallback, and resampling limits are documented. |
| False successes | Reference agreement can coexist with inaccurate registration. Truth is deliberately diagnostic, except the full-reference guard; that is an explicit limitation. |
| Measurement validity | Primary accounting is defective. Sampled acceptance does not independently validate every denominator or PX moment. |
| Comparison design | Paired arms and fixed thresholds are specified. Six cells are descriptive coverage, not six independent fixture families. Baseline-only timing repeats cannot characterize every arm’s variability. |
| Execution | Per-cell timeout exists; scientific stop rules and frozen-state resumption checks do not. Technical-test claims were not independently checked. |

Prospective control outcomes are acceptable under the requested combined pilot/confirmation treatment. I am **not** requiring a separate pilot review or already-observed control results; the plan must specify consistent, enforceable validity decisions.

**4. Optional findings**

CPU pinning, reordered timing repetitions, and per-iteration landmark histories would improve interpretation, but their absence is already acknowledged and need not expand this task. The task’s example command also contains unsupported `--positions`; correct that documentation ([task:29](/home/alex/Documents/IntrinsicEngine/tasks/active/METHOD-077-cpd-nystrom-landmark-variant-experiment.md:29)).

No writes, builds, experiment runs, CLIs, delegation, external-file reads, or result-file inspection occurred. The numbered checklist remains open; the established blockers already rule out PASS.
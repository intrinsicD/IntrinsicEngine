---
id: METHOD-077
theme: I
depends_on: [METHOD-076]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive experiment; its evidence is the reviewed pre-flight packet and the run outputs under ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/.
contract_schema: 1
contracts: []
contract_review: benchmark runner and experiment only; no engine surface or catalog contract changes.
---
# METHOD-077 — Compare the Nystroem landmark variants for Bayesian CPD

## Goal
- Answer operator question 1 (quoted in the packet's Layer B) for synthetic fixtures at 10^5
  points with Bayesian CPD: which landmark variant registers fastest (wall time, operator
  decision) while staying equivalent to the exact E-step.

## Acceptance criteria
- [x] Full-tier pre-flight packet (Layer A, Layer B, manifest) committed; two independent
      reviews (Claude, Codex) PASS on the same digest.
- [x] Run plan executed by `tools/benchmark/cpd_landmark_variants.py`; raw outputs kept.
- [x] Referee audit from raw outputs: decision per variant, predictions P1-P7, controls.

## Verification
```bash
python3 tools/benchmark/cpd_landmark_variants.py --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants --out /tmp/plan --plan
python3 tests/regression/tooling/Test.CpdLandmarkVariantsAnalysis.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-09 technical test (not evidence): the runner completes `exact` and a Nystroem variant
  at 10^4 and 10^5 points; about 106 s and 88 s at 10^5, peak about 394 MB; every run reaches the
  150-iteration cap.
- 2026-10-09 pre-flight round 1 on digest `de8b3cc3…fa56` (Codex thread
  `01a121ef-db33-7601-9b89-5051a6b9d62a`, Claude session `656249ac-d122-4bb0-9113-4e6974f66001`;
  an earlier Codex attempt on `345d6fc0…` was blocked by its own approval check before reading
  and the Claude reconstruction of that digest drove the first hardening): both "REVISE". Operator
  decisions recorded in Layer B item 4. Revision: wall time with two passes (reverse order,
  pinned to one CPU per core) as the decision metric, perturbation control, landmark seed =
  fixture seed, gates and decision rule implemented in the frozen `--analyze` with its test
  (mutation-checked: noise condition, determinism gate), resume bound to commit, runner and size,
  lock file, accurate descriptions of `exact`, the controls and the kernel counter.
- 2026-10-09 delta round on digest `b35c9172…af6e`: both "REVISE" (Codex: missing-arm gate,
  unevaluable predictions counted as passed, analysis provenance, optional pinning, artifact
  deletion, text; Claude: one-sided outcomes, perturbation engagement, configuration
  justification, union-distribution test). Fixed: outcome-equality gate, perturbation engagement
  gate, noise condition for "worse", "not evaluable" predictions, provenance and pinning checks in
  the analysis, required `taskset`, artifacts moved to `runs/stale/`, analysis versions, kernel
  rank and error in the runner output, configuration choices in Layer B, a union-uniformity test
  (a first version counted one draw 100 times through the rejection memory; fixed to independent
  draws; mutation: dropping the overlap correction fails it). Analysis test: 8 cases, mutations of
  the outcome gate, perturbation gate and provenance check each fail it.
- 2026-10-09 second delta round on digest `2a83e709…074d`: Claude "PASS"; Codex "REVISE" (P5 with
  missing variant measurements; analysis not bound to the expected commit and runner). Fixed: P5
  not evaluable unless all nine variants are measured; the analysis needs `--runner`, compares
  every record with HEAD and that runner's sha256 and refuses missing planned cells;
  `max_to_exact` in the analysis rows; wording.
- 2026-10-09 third delta round on digest `7bb6c425…4e3f` (commit `74a198c0e`): Codex "PASS",
  Claude "PASS". Start gate: live manifest digest equal, tree clean.
- 2026-10-10 run: 138/138 cells completed (21:56-00:48). Frozen analysis: no ranking; the exact
  gate fails on all `full` pairs (exact `rms_to_truth` about 0.157 > 0.02; scale 1.011 vs 1.1);
  Nystroem engages in 0-6 of 150 iterations. Surprise: the remaining plan is void. Audit in
  `ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/audit.md`.

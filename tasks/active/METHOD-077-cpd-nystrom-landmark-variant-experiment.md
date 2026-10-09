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
  points with Bayesian CPD: which landmark variant computes P with the fewest kernel
  evaluations while staying equivalent to the exact E-step.

## Acceptance criteria
- [ ] Full-tier pre-flight packet (Layer A, Layer B, manifest) committed; two independent
      reviews (Claude, Codex) PASS on the same digest.
- [ ] Run plan executed by `tools/benchmark/cpd_landmark_variants.py`; raw outputs kept.
- [ ] Referee audit from raw outputs: decision per variant, predictions P1-P6, controls.

## Verification
```bash
python3 tools/benchmark/cpd_landmark_variants.py --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants --out /tmp/plan --positions /tmp/plan --plan
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-09 technical test (not evidence): the runner completes `exact` and a Nystroem variant
  at 10^4 and 10^5 points; about 106 s and 88 s at 10^5, peak about 394 MB; every run reaches the
  150-iteration cap.

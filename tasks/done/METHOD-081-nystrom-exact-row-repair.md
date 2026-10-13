---
id: METHOD-081
theme: I
depends_on: [METHOD-080]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation and bounded experiment; fixed-diff reviews, tests and reviewed run packet live with task evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-081 — Repair invalid Nyström target rows exactly

## Goal
1. "Also heißt das identifizieren und exakt nachrechnen?"
2. "Dann versuchen wir es damit mal."
3. "Mach dann nochmal einen Test bitte"

Operator-directed work outside Framework24 P0. [Plan](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-081/plan.md)
implements the agreed complete-row repair and tests it on analytic controls and the known fixture.

## Acceptance criteria
- [x] Invalid target rows are replaced consistently in every E-step statistic, with weighted shifts, deterministic reduction, existing accuracy checks and bounded repair cost.
- [x] Focused accelerated/Bayesian tests pass; new regression and mechanism-removal checks demonstrate the repair and retained guards.
- [x] Independently reviewed bounded comparison, raw HTML report and audit state accepted iterations, accuracy, timing and remaining limits without claiming universal positivity.

## Contract matrix
| Contract | Scope |
|---|---|
| method.engine-integration | Shared CPU E-step implementation behind existing CPD/BCPD public APIs; no new selection/config/publication surface. |

## Engine integration
| Field | Decision |
|---|---|
| Least-structured input | Existing 3-D point spans and optional finite source log weights. |
| Compatible entity sources | Existing point/vertex/property bindings; no eligibility or topology change. |
| RuntimeModule | Existing coherent-point-drift operation uses the shared evaluator. |
| Config/agent | Existing validated Nyström settings; no new setting. |
| UI | Existing backend selection; no control change. |
| Publication | Existing transformed positions and diagnostics; cardinality unchanged. |
| End-to-end tests | Existing accelerated and Bayesian registration suites exercise the shared path; existing runtime integration contract is unchanged. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(CoherentPointDriftAccelerated|CoherentPointDriftBayesian)\.'
python3 tools/repo/check_layering.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Completion — 2026-10-11
Implementation commit: `ba11fce6b`; frozen comparison: `7c49f5f14`; baseline: `a93dcb8c7`.
Canonical result and limits: [ARA C122](../../ara/logic/claims.md),
[raw report](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-081/runs/report.html).

Independent code reviewer `/root/nystrom_repair_code` accepted the first two criteria on
diff SHA256 `a2f0d3daa82a58f116b1982a478ea241d57e26220114cdfdab0f8c60c8b3af4b`.
Referee `/root/method081_results` decided: **“approve — I accept METHOD-081’s third acceptance
criterion for this bounded CPU comparison.”** Its [actual answer](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-081/reviews/results-review.txt)
and [claim wording verdict](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-081/reviews/claim-review.txt) bind the raw audit.
Two fresh Codex reviewers approved preflight after the operator's explicit Claude substitution.

Focused evidence is 41/41 tests, two before-fix regression failures and four rejected
implementation mutations. Complete report controls also reject bypassed verdict guards.
All ten scientific cells completed. Box fallback diagnosis, longer prefixes, full CPU,
sanitizer and GPU checks remain outside this slice. Main CI and the unchanged 24 METHOD-077
review-link failures remain owned by BUG-240/BUG-241; no broader CI repair was attempted.

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

Operator-directed work outside Framework24 P0. [Plan](../evidence/METHOD-081/plan.md)
implements the agreed complete-row repair and tests it on analytic controls and the known fixture.

## Acceptance criteria
- [x] Invalid target rows are replaced consistently in every E-step statistic, with weighted shifts, deterministic reduction, existing accuracy checks and bounded repair cost.
- [x] Focused accelerated/Bayesian tests pass; new regression and mechanism-removal checks demonstrate the repair and retained guards.
- [ ] Independently reviewed bounded comparison, raw HTML report and audit state accepted iterations, accuracy, timing and remaining limits without claiming universal positivity.

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

## Context
Existing dirty ARA notes record the preceding PSD/remedy discussion. Baseline is `a93dcb8c7`.
Main CI is already red; BUG-240/BUG-241 retain the prior failure evidence. No broader CI repair here.

Independent code reviewer `/root/nystrom_repair_code` accepted the first two criteria on
diff SHA256 `a2f0d3daa82a58f116b1982a478ea241d57e26220114cdfdab0f8c60c8b3af4b`.
Experimental approval and audit remain pending.

Focused verification: 41/41 tests; both new regressions fail before repair and four
mechanism-removal mutations fail. Structural checks pass except the unchanged 24 METHOD-077
review links owned by BUG-241. Two 100000-point technical checks passed without inspecting
their method outcomes. Full experiment preflight is pending: Claude's weekly quota prevented
its review; the operator has been asked whether a second independent Codex reviewer may
substitute. No results-bearing comparison has started.

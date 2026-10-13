---
id: METHOD-078
theme: I
depends_on: [METHOD-077]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive micro diagnosis; research evidence and the reviewed frozen pilot are retained under tasks/evidence/METHOD-078.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-078 — Diagnose Nyström rejection and evaluate retrying

## Goal
1. "Dann bitte nyström Nutzung untersuchen und verbessern"

Continue METHOD-077. Prefer less total registration work at unchanged accuracy over a larger
accepted-iteration counter. Diagnose the first rejection and the subsequent suppression before
changing production selection policy. The operator may clarify this priority.

## Acceptance criteria
- [x] Distinguish numerical/accuracy rejection, cost rejection, and cached rejection in the actual METHOD-077 configuration.
- [x] Test a candidate against unchanged accuracy/fallback controls; preserve the baseline and reject unsupported speedup claims.
- [x] Remove temporary diagnostic probes before delivering any production change; record what remains uncertain.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged two point-position sets. |
| Compatible entity sources | Unchanged mesh vertices, graph nodes and point clouds. |
| RuntimeModule | Existing RegistrationOperations CPD; no new wiring. |
| Config/agent | Existing Nyström source, schedule and accuracy fields; no new fields. |
| UI | Existing CPD panel; no new controls. |
| Publication | Existing transformed-source positions, same cardinality. |
| End-to-end tests | Existing public Register path in the landmark runner; runtime Nyström tests before any production adoption. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^CoherentPointDriftAccelerated\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Completion — 2026-10-11

Bounded diagnosis completed; no production improvement adopted. Frozen execution commit:
`0efb7e48f87ee57fd5cb34f2c5d072fb71391d3e`; production files restored to `bbdf1af79`.
Canonical findings and limits: [audit](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-078/audit.md), ARA C119.
The existing METHOD-077 experiment remains separate and unchanged.

Independent reviewer `/root/nystrom_results_review` decided:
**“Verdict: Ship. All three METHOD-078 acceptance criteria PASS.”**
[Actual answer and exact artifact bindings](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-078/reviews/cleanup-review.md).
Cleanup build and 28/28 original Accelerated tests pass; strict doc links retain
pre-existing BUG-241. Candidate, raw results and frozen report remain preserved.

Further investigation of non-positive/nonfinite target sums is a new diagnostic layer,
awaiting the operator's scope decision under the global drift rule. Broader CPD variant
work remains with METHOD-077; no runtime/backend capability change was delivered.

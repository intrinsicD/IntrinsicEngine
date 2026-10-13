---
id: METHOD-079
theme: I
depends_on: [METHOD-078]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive diagnosis; frozen reviewed pilot, raw snapshots and independent audit retained with task evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-079 — Diagnose the first invalid Nyström target sum

## Goal
1. "Was genau ist hier das Problem?"
2. "Ok dann bitte tue das."

Operator authorizes the next diagnostic layer proposed after METHOD-078: compare the first
invalid target sum with an exact sum, landmark distances / sigma and the retained spectrum.
[Full question and plan](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-079/plan.md). All steps serve these questions.
No production stabilization, changed tolerance, retry policy or larger budget is selected here.

## Acceptance criteria
- [x] Capture the deterministic first invalid target row and its actual operands in both bounded variants.
- [x] Independently recompute exact/approximate sums, spectrum and coverage; retain controls, uncertainty and any unresolved mechanism.
- [x] Publish the reviewed raw-output report and remove temporary production probes; original focused tests pass.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Existing point-position sets; no contract change. |
| Compatible entity sources | Existing mesh vertices, graph nodes and point clouds. |
| RuntimeModule | Existing CPD registration; diagnostic only, no new wiring. |
| Config/agent | Existing method configuration; temporary capture removed before delivery. |
| UI | Existing CPD controls unchanged. |
| Publication | Existing transformed source, same cardinality. |
| End-to-end tests | Actual Register path in existing landmark runner; snapshot is temporary, no capability promotion. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^CoherentPointDriftAccelerated\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Completion — 2026-10-11

Canonical result and limits: [audit](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-079/audit.md), ARA C120.
Frozen execution commit: `cae9a475690c5b5285d523403ba71689b6d21007`.
The frozen four-cell diagnostic completed; no production fix or guard relaxation was adopted.
Both temporary production files are restored exactly to `0631ef12c`.

Independent reviewer `/root/nystrom_first_row_audit` decided:
**“PASS — bounded first-row diagnosis. Mandatory findings: none.”**
Its [actual answer](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/tasks/evidence/METHOD-079/reviews/results-review.txt) explicitly finds
acceptance criteria 1–3 met. The `ci` cleanup build and 28/28 original Accelerated cases pass.
Strict doc links retain pre-existing BUG-241; full CPU/sanitizer/Vulkan were not rerun.

A production remedy and any wider census are the next scope decision under the global drift
rule. Existing METHOD-077 variants/evidence remain unchanged.

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
[Full question and plan](../evidence/METHOD-079/plan.md). All steps serve these questions.
No production stabilization, changed tolerance, retry policy or larger budget is selected here.

## Acceptance criteria
- [ ] Capture the deterministic first invalid target row and its actual operands in both bounded variants.
- [ ] Independently recompute exact/approximate sums, spectrum and coverage; retain controls, uncertainty and any unresolved mechanism.
- [ ] Publish the reviewed raw-output report and remove temporary production probes; original focused tests pass.

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

## State
Independent plan reviews and their scoped dispositions are in the evidence directory.
Preparation is implemented; scientific execution requires the new frozen packet's two passing reviews. Existing red main CI and BUG-241 are pre-existing; no gates are weakened.

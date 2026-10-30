---
id: METHOD-083
theme: I
depends_on: [METHOD-082]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation and original-program comparison; reviews, controls, frozen packet and raw report accompany task evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-083 — Align Nyström mechanisms with the author implementation

## Goal
1. "Bitte pass unsere implementieren entsprechend dieser Punkte des Original Codes an. Dann wiederhole den Vergleich, auch mit dem Original."

Operator-directed work outside Framework24 P0. Adapt the shared Nyström path and compare
the previous engine, adapted engine and pinned author executable on the existing full/
partial fixtures. Earlier "Ok ohne Claude" remains in force. [Plan](../../evidence/METHOD-083/plan.md).

## Acceptance criteria
- [x] Regularized solve, union-random landmarks and early neighbor transition have analytic/regression controls, deterministic behavior and truthful diagnostics.
- [x] Public/config/agent/UI callers round-trip all changed controls; method docs describe retained differences from upstream.
- [x] Focused tests and independent code review pass, including before-change/mutation evidence for new behavior.
- [ ] Reviewed frozen comparison includes the unmodified author program; complete raw reports, convergence/quality/coverage diagnostics and independent audit retain all results and limitations.

## Engine integration
| Field | Contract |
|---|---|
| Least-structured input | Existing 3-D spans and optional finite source log weights. |
| Compatible entity sources | Existing typed point/vertex/domain binding; no eligibility change. |
| RuntimeModule | Existing registration operation maps the shared validated settings. |
| Config/agent/UI | Extend existing fields/enums and panel; round-trip/preview validation remains canonical. |
| Publication | Same-cardinality transformed positions and existing backend/error diagnostics. |
| End-to-end tests | Accelerated, Bayesian and runtime registration suites; no new publication path. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(CoherentPointDriftAccelerated|CoherentPointDriftBayesian)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
Runtime test target and experiment commands are resolved in the plan/packet before execution.

Independent `/root/method083_code` accepted AC1–AC3 for implementation patch
`453688ab401e2d47bf0f16ac178baa9237ede1f6e52d728bba2ee03330ac479b`;
[review](../../evidence/METHOD-083/code-review.md). AC4 remains open; no experiment approval yet.

## Log
- 2026-10-30: Moved from `tasks/active/` to the backlog by the operator's decision (ARCH-022, question 38): nobody is working on it now. Its stale claim fields were cleared; its scope, state and open points are unchanged.

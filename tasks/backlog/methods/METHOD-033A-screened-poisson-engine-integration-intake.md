---
id: METHOD-033A
theme: I
depends_on: [METHOD-033, REVIEW-004]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive follow-up planning note; implementation and research evidence are outside this intake.
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
---
# METHOD-033A — Screened Poisson engine-integration intake and ownership

## Goal

Own METHOD-033's deferred runtime/config/UI and non-destructive mesh
publication decisions while leaving its CPU reference and METHOD-034 baseline
scope unchanged.

## Context

This named follow-up is required by the current integration contract when the
parent task is revised. It is an interactive planning note, not a night-ready
implementation task or permission to promote an unvalidated reconstruction
variant. REVIEW-004 and the accepted parent contract gate selection.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Paired finite float position/normal properties or spans on one element domain, satisfying the accepted reconstruction input contract. |
| Compatible entity sources | Compatible mesh vertex/face/edge/halfedge, graph node/edge/halfedge, and point-cloud point sources; no provenance conversion or property-name alias. |
| RuntimeModule | This intake selects the existing operation owner and seeds a bounded binding task after CPU acceptance. |
| Config/agent | Define serializable params and the shared side-effect-free preview/validate/apply path. |
| UI | Scope discovery under every compatible property domain using runtime readiness. |
| Publication | A new mesh with explicit ownership/history; preserve the original entity's properties/topology and reject stale completion. |
| End-to-end tests | Allocate per-source binding, input-quality distinction, config/UI parity, stale/failure, and publication/history coverage to named implementation tasks. |

## Acceptance criteria

- [ ] Review the accepted METHOD-033 input/quality/failure contract and select
      a concrete production consumer after the product gate.
- [ ] Confirm the control-surface and publication decisions with the operator;
      iterative intermediate support does not imply final surface quality.
- [ ] Seed named runtime/config/UI/publication implementation tasks and update
      the parent's deferred matrix rows before retiring this intake. No
      editor-integrated capability is claimed by this planning record.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
```

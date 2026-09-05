---
id: METHOD-007A
theme: I
depends_on: [METHOD-007, REVIEW-004]
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
# METHOD-007A — CDT engine-integration intake and ownership

## Goal

Own the deferred runtime/config/UI/publication decisions from METHOD-007
without expanding its CPU-reference slice or starting speculative engine work.

## Context

The 2026-09-05 backlog revision enrolls METHOD-007 in the current integration
contract. This note supplies a named follow-up owner; it is not an approved
volumetric editor design or a night-ready implementation task. Research work
remains behind REVIEW-004 and an accepted CPU reference.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Closed triangle boundary with finite float positions and METHOD-007's accepted PLC contract. |
| Compatible entity sources | Surface meshes satisfying that contract; no point-cloud or graph-only shortcut. |
| RuntimeModule | This intake selects the existing composition owner and seeds the implementation task before closure. No new service is assumed. |
| Config/agent | Scope serializable typed params and shared preview/apply with the implementation owner. |
| UI | Establish the concrete volumetric workflow and domain discovery before allocating UI work. |
| Publication | Decide ownership/persistence of the separate TetMesh and explicit history behavior; never silently replace the source boundary. |
| End-to-end tests | Allocate source/readiness, config parity, failure, persistence, and publication/history tests to named implementation tasks. |

## Acceptance criteria

- [ ] Inspect the accepted METHOD-007 result/API and identify a concrete engine
      consumer after the standing product gate is satisfied.
- [ ] Confirm the integration decisions with the operator; preserve float
      public storage and post-conversion validity checks.
- [ ] Seed named, bounded implementation tasks for runtime/config/UI and
      publication/tests; update METHOD-007's matrix to those owners before
      retiring this intake. This note alone never counts as engine integration.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
```

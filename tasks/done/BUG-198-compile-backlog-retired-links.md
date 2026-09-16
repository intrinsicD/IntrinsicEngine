---
id: BUG-198
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive documentation repair; strict lifecycle/link checks and the retained failing output are the evidence.
contract_schema: 1
contracts: []
contract_review: Task indexes only; no source interface, task schema, workflow or catalog contract changes.
---
# BUG-198 — Remove retired compile tasks from open indexes

## Goal
Restore accurate open-task indexes without removing completed task history.

## Context
The strict lifecycle-link check at GRAPHICS-145 retirement found five links in the
root backlog table and two in the runtime category that presented retired work
inside open sections. All except the new GRAPHICS-145 link predate this turn.
The captured failing output is retained with GRAPHICS-145 diagnostics.

## Acceptance criteria
- [x] Remove retired task rows from the open compilation table; preserve retirement records.
- [x] Remove the stale runtime category entry and correct its completed-work sequencing.
- [x] Pass strict lifecycle and documentation-link checks without changing their rules.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Completion — 2026-09-16
Retired as a documentation repair; engine maturity is unchanged. Commit reference:
the accompanying retirement/evidence commit; reproduced after checkpoint `f73dcbd39`.
[Failing check](../../ara/evidence/diagnostics/graphics145_renderer_frontend/first-final-checks.txt)
and the adjacent final checks record the correction. No deferred work.

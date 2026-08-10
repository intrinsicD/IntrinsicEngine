---
id: BUG-152
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Single category-index lifecycle-link correction with no engine, workflow-policy, or behavior change; strict task-state and task validators are the complete proof."
template: micro
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "This is a task-lifecycle index defect governed by the reusable task discovery/state-link contract. Engine layering, method integration, publication, runtime, config, UI, and backend contracts are not involved."
---
# BUG-152 — Geometry index presents retired GEOM-071 as active backlog work

## Goal
- Make the geometry backlog index classify its retired `GEOM-071` link as
  history so strict task-state link validation agrees with the task's location.

## Non-goals
- No reopening or changing `GEOM-071`, its source implementation, evidence,
  maturity, retirement record, or downstream dependencies.
- No geometry, method, runtime, config, UI, backend, or build behavior change.

## Context
- Symptom: on 2026-08-11,
  `python3 tools/agents/check_task_state_links.py --root . --strict` failed at
  `tasks/backlog/geometry/README.md:45` because the active-list section links
  `GEOM-071` into `tasks/done/` outside a history-marked heading.
- Expected behavior: links to retired tasks appear under a recognized history
  section or as non-link historical text, while active backlog entries remain
  linked from the active section.
- Impact: the repository-wide strict documentation/task gate is red on the
  unchanged `92fd3ecb` baseline, independently of the observing `BUG-151`
  workflow repair.

## Required changes
- [ ] Move or rewrite the `GEOM-071` category-index entry so its retired state
      is explicit and the active `GEOM-072` dependency narrative remains clear.

## Tests
- [ ] Run the strict task-state-link checker and task-policy validation.

## Docs
- [ ] Keep the geometry category index and generated session brief synchronized.

## Acceptance criteria
- [ ] Strict task-state-link validation passes without changing `GEOM-071` or
      weakening the checker.
- [ ] `GEOM-072` still resolves its dependency on the retired task.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Editing `GEOM-071` source, evidence, completion report, or retirement status.
- Weakening task-state validation or relabeling active work as history.

# HARDEN-065 — Task checkbox todo policy

## Status
- done
- Completion date: 2026-05-11
- Owner: agent
- Commit reference: pending commit.

## Goal
Make open and completed task work visible at a glance by standardizing actionable task sections on markdown checkbox todos.

## Non-goals
- No changes to task scope, task ordering, or subsystem ownership.
- No new product/runtime behavior.
- No conversion of context, non-goal, or forbidden-change bullets into todos.

## Context
- Owner/layer: agent workflow and task policy.
- Existing structured tasks already used the required section shape, but most actionable sections used plain bullets that could not show open vs completed state.
- `tasks/done/` records completed work; unresolved work from completed records should be captured by follow-up task files instead of unchecked todos.

## Required changes
- [x] Update task-format documentation to require checkbox todos in actionable sections.
- [x] Update task templates so new tasks start with markable todos.
- [x] Convert existing actionable task bullets to `- [ ]` for open task records and `- [x]` for completed task records.
- [x] Extend task validation to require checkbox todos and reject unchecked actionable todos in completed task records.

## Tests
- [x] Run strict task-policy validation.
- [x] Run documentation link validation.
- [x] Run whitespace validation on the final diff.

## Docs
- [x] Update `docs/agent/task-format.md`.
- [x] Update `tasks/README.md`.
- [x] Update `tools/agents/README.md` for the checker behavior.

## Acceptance criteria
- [x] New task templates use checkbox todos in `Required changes`, `Tests`, `Docs`, and `Acceptance criteria`.
- [x] Existing structured task records expose markable todos in actionable sections where possible.
- [x] `tools/agents/validate_tasks.py` enforces the convention for structured task files.
- [x] Completed task records have no unchecked actionable todos.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Rewriting task goals or changing backlog priorities.
- Mixing task-policy cleanup with engine code changes.
- Marking open backlog tasks as completed.

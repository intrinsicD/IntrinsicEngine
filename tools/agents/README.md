# tools/agents

Agent workflow and task policy tooling.

## Current state

- `check_task_policy.py` validates required task directories, rejects legacy root planning files, and delegates strict structured-task checks.
- `validate_tasks.py` validates task IDs, required sections, completion metadata for `tasks/done/`, and checkbox todos in actionable sections.

## Planned moves

- Move compatibility wrapper script(s) into this directory where needed (RORG-071).
- Add method manifest validation scripts under this ownership boundary (RORG-041).

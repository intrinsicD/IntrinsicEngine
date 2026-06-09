# tools/agents

Agent workflow and task policy tooling.

## Current state

- `check_task_policy.py` validates required task directories, rejects legacy root planning files, and delegates strict structured-task checks.
- `check_task_maturity_followups.py` validates that open backend-facing `CPUContracted` maturity closures name an operational owner or explicitly state that no operational follow-up is owed.
- `check_task_state_links.py` validates that task links and nearby lifecycle status claims agree with the actual `tasks/backlog/`, `tasks/active/`, and `tasks/done/` location of the referenced task ID.
- `validate_tasks.py` validates task IDs, required sections, completion metadata for `tasks/done/`, and checkbox todos in actionable sections.
- `sync_skills.py` mirrors canonical `docs/agent/*` (plus `tasks/templates/task.md`) into the physical skill root `tools/agents/skills/`, rewriting relative links for the mirror location. `.claude/skills` and `.codex/skills` are symlinks to that root. `--write` regenerates; `--check` (the `ci-docs.yml` gate) fails on any divergence, missing file, or broken skills symlink. `resync_skills.sh` is a thin `--write` wrapper.

## Planned moves

- Move compatibility wrapper script(s) into this directory where needed (RORG-071).
- Add method manifest validation scripts under this ownership boundary (RORG-041).

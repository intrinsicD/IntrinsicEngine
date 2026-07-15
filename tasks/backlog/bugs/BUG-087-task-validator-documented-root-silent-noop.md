---
id: BUG-087
theme: G
depends_on: []
---
# BUG-087 — Documented task-validator root silently validates zero tasks

## Goal
- Make the documented local task-validation command validate the repository's
  real `tasks/` tree and make a mistaken root fail closed instead of reporting
  a success-shaped zero-file result.

## Non-goals
- No task metadata/schema redesign.
- No relaxation of strict task-policy or task-state-link validation.
- No hand-editing generated skill reference mirrors.

## Context
- Claimed on 2026-07-16 by Codex on branch
  `codex/arch-006-completion`.
- Symptom: `docs/agent/task-format.md` and its generated task-workflow skill
  instruct agents to run `python3 tools/agents/validate_tasks.py --root .`.
  On 2026-07-15 that command exited zero with `No task markdown files found`
  because the script interprets `--root` as the task-root directory and looks
  for `./active`, `./backlog`, and `./done`.
- Expected behavior: the documented command validates the repository task tree,
  and strict validation rejects an accidental zero-file discovery.
- Impact: agents can cite a green no-op while malformed task files remain
  unchecked. The effective command
  `python3 tools/agents/validate_tasks.py --root tasks --strict` validated 73
  task files with zero findings in the same session.

## Required changes
- [ ] Choose and document one unambiguous root contract for
      `validate_tasks.py` (`--root tasks` or repository-root discovery).
- [ ] Make strict mode return nonzero when no task Markdown files are found,
      with a diagnostic that names the searched directories.
- [ ] Correct every canonical invocation and regenerate the task-workflow skill
      mirrors through `tools/agents/sync_skills.py --write`.

## Tests
- [ ] Add a tooling regression proving the canonical repository invocation
      discovers task files and a wrong empty root fails nonzero under `--strict`.

## Docs
- [ ] Update `docs/agent/task-format.md` and any other canonical command bundles;
      regenerate, rather than hand-edit, generated skill references.

## Acceptance criteria
- [ ] The documented command validates a nonzero task count in this repository.
- [ ] Strict validation cannot succeed after discovering zero task files.
- [ ] Task-policy, task-state-link, docs-sync, and skill-mirror checks remain
      green.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Preserving a success exit for zero discovered tasks in strict mode.
- Weakening task validation to make the corrected invocation green.

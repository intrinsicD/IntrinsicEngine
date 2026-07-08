---
id: PROC-017
theme: H
depends_on: []
---
# PROC-017 — Document branch naming, CI-failure intake, claiming, and batch-seed conventions

## Status

- Status: done (retired 2026-07-08). Documented in the same PR that seeded
  the task (agentic-workflow review batch on
  `claude/agentic-workflow-skills-review-tbnb0b`).
- PR/commit: this retirement commit on
  `claude/agentic-workflow-skills-review-tbnb0b`.
- Origin: agentic-workflow review (2026-07-08): four lifecycle conventions
  were exercised in practice but written down nowhere.

## Goal

- Write down the four undocumented conventions the workflow already relies
  on: branch naming, converting CI failures into `BUG-` tasks, claiming a
  task against concurrent sessions, and batch-seeding task IDs safely.

## Non-goals

- No new process obligations beyond what retired tasks already practiced
  (`BUG-062`/`063`/`064` for CI intake, `PROC-002`/`PROC-012` for ID
  collisions).
- No enforcement tooling; `drift-audit-checklist.md` Row 3 and
  `validate_tasks.py` remain the existing checks.

## Context

- `drift-audit-checklist.md` audits branch drift against a naming convention
  that was never defined; active-task files ask for a branch reference with
  no scheme.
- CI flakes were converted into `BUG-` tasks by practice
  (`BUG-062`/`BUG-063`/`BUG-064`) but no doc said to do that, and `BUG-063`
  records the risk of lingering red gates.
- The historical duplicate IDs (`BUG-021`/`BUG-022` ×2, `HARDEN-065` ×3, and
  the post-validator `GEOM-027` collision fixed by `PROC-012`) trace to
  concurrent sessions with no claim signal and to batch seeding without a
  local uniqueness run.

## Required changes

- [x] Add a "Claiming" paragraph to `docs/agent/prompt/prompt.md` §"Pick the
      next slice" (owner/branch recorded in the task file is the claim
      signal).
- [x] Add a "When CI fails" section to `docs/agent/prompt/prompt.md`
      (own-change fix, pre-existing→`BUG-` task with evidence, red `main`
      outranks feature work) and an anchoring bullet in `AGENTS.md` §10.
- [x] Add the branch-naming convention
      (`<owner>/<task-id-lowercase>-<short-slug>`, harness names acceptable,
      task-file record is what audits check) to `prompt.md` §"Commit and PR
      hygiene".
- [x] Extend `docs/agent/task-format.md` §"ID allocation" with the canonical
      prefix-list pointer and the batch-seeding rule (allocate ranges up
      front, run `validate_tasks.py` locally, first-merged keeps the
      numbers).

## Tests

- [x] `python3 tools/agents/sync_skills.py --write` re-run so the
      `prompt.md`/`task-format.md` mirrors stay fresh; `--check` passes.

## Docs

- [x] All changes are docs; no separate update owed.

## Acceptance criteria

- [x] Each of the four conventions is documented exactly once, in the file
      its lifecycle step already routes to.
- [x] `AGENTS.md` gains no operational detail beyond the §10 anchor —
      single-sourcing preserved.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Enforcement/CI changes (owned by `PROC-021` where relevant).
- Embedding task-specific policy into `prompt.md`.

---
id: PROC-014
theme: H
depends_on: []
maturity_target: Retired
completed_on: 2026-07-07
---
# PROC-014 — Task-state index done-link cleanup

## Goal
- Restore the task-state link check by removing direct links from live backlog
  sections to retired task files.

## Non-goals
- No task dependency, status, or scope changes.
- No changes to engine code, generated inventories, or retired task narratives.
- No merge of independently scoped runtime or rendering backlog tasks.

## Context
- Owner/layer: process / task tree hygiene.
- `python3 tools/agents/check_task_state_links.py --root . --strict` reported
  retired task links under live sections in `tasks/backlog/rendering/README.md`
  and `tasks/backlog/runtime/README.md`.
- Category READMEs may link retired tasks only from history-marked sections or
  explicitly exempt dependency DAGs. Live sections should cite retired tasks as
  plain code spans so agents do not mistake them for selectable backlog work.
- Completed: 2026-07-07. Commit: this commit.

## Required changes
- [x] Convert the live "Current open rendering leaves" `GRAPHICS-119` retired
      task link to a plain code-span reference.
- [x] Convert the live runtime non-blocking/composition cleanup references to
      retired `RUNTIME-140..145` tasks from links to plain code-span references.

## Tests
- [x] `python3 tools/agents/check_task_state_links.py --root . --strict`
      reports zero findings.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` reports
      zero findings.
- [x] `python3 tools/docs/check_doc_links.py --root .` reports zero findings.

## Docs
- [x] `tasks/backlog/rendering/README.md` and
      `tasks/backlog/runtime/README.md` now describe the retired work without
      live-section links into `tasks/done/`.
- [x] `tasks/done/RETIREMENT-LOG.md` records this retirement.

## Acceptance criteria
- [x] No live backlog section links directly to the retired tasks identified by
      `check_task_state_links.py`.
- [x] The rendering and runtime category READMEs still communicate the same
      current-state facts.
- [x] The cleanup stays task/docs-only.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
```

## Forbidden changes
- Changing open task priority, dependencies, maturity, or scope.
- Moving retired tasks back into active/backlog state.
- Editing engine source.

## Maturity
- Target: `Retired`. This is a completed process hygiene cleanup; no follow-up
  task is owed.

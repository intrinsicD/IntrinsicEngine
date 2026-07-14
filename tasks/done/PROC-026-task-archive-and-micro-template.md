---
id: PROC-026
theme: H
depends_on: []
---
# PROC-026 — Sweep retired tasks to an archive and add a micro task template

## Goal
- [x] Shrink the live task working set without losing the agentic-work record:
  retired tasks move to a frozen `tasks/archive/`, and single-slice mechanical
  work gets a reduced `template: micro` task format.

## Non-goals
- No change to the retirement flow itself (tasks still retire to
  `tasks/done/` with date + commit/PR reference + retirement-log entry).
- No deletion of history; archived files remain in the tree, read-only.
- No relaxation of full-template requirements for architecture, module-surface,
  method/benchmark, or maturity-ambiguous work.

## Context
- 2026-07-14 process review: `tasks/done/` had grown to 661 retired files
  (~90% of the validated task set), taxing every validator pass and
  session-start listing while the working set was ~70 files. The nine-section
  template had organically become mandatory even for trivial mechanical
  slices.
- Owner: task tooling (`tools/agents/*`), task tree, `docs/agent/task-format.md`.

## Required changes
- [x] Move all retired task files from `tasks/done/` to `tasks/archive/`
  (README.md and RETIREMENT-LOG.md stay; inbound links rewritten repo-wide).
- [x] Keep archived IDs authoritative: duplicate-ID detection, `depends_on`
  resolution, and state-link guards treat archive as done
  (`validate_tasks.py`, `generate_session_brief.py`,
  `check_task_state_links.py`).
- [x] Add `tasks/templates/task-micro.md` and the `template: micro`
  front-matter marker with reduced required sections
  (Goal / Acceptance criteria / Verification).

## Tests
- [x] Strict `validate_tasks.py` passes post-sweep (67 files, 0 findings);
  probe micro task passes strict, fails when `## Verification` is removed,
  and resolves a `depends_on` pointing at an archived ID.

## Docs
- [x] `tasks/archive/README.md` (rules + sweep policy), `tasks/README.md`,
  `tasks/done/README.md`, `AGENTS.md` §11 note,
  `docs/agent/task-format.md` (micro section, retiring, ID allocation);
  skill mirrors resynced.

## Acceptance criteria
- [x] `tasks/done/` contains only README.md + RETIREMENT-LOG.md + recent
  retirements; all validators run strict and clean; SESSION-BRIEF content
  unchanged by the sweep.
- [x] `check_doc_links` reports zero broken relative links after the sweep.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- Editing archived task bodies.
- Weakening any check that currently runs strict in CI.

Completed: 2026-07-14. Commit: 865a61b (archive sweep) and b6d4568 (micro
template) on `claude/intrinsic-framework24-comparison-ospg0l`.

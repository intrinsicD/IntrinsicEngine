---
id: PROC-008
theme: H
depends_on: [PROC-003]
---
# PROC-008 — Category README state/history split

## Goal
- Apply the PROC-003 state-vs-history split to the per-category backlog READMEs (`tasks/backlog/<category>/README.md` and `bugs/index.md`): open tasks in the main lists, retired entries under an explicit history heading, so the state-only link guard can be extended to category indexes.

## Non-goals
- No changes to `tasks/active/README.md` or the top-level backlog README (owned by retired `PROC-003`).
- No deletion of any retired-entry line — entries move under a history heading or to the retirement log, verbatim.
- No changes to theme definitions or dependency semantics.

## Context
- Owner/layer: task indexes (category READMEs) and `tools/agents/check_task_state_links.py`. No engine code.
- `PROC-003` cleaned the two mandatory session-start indexes and scoped the regrowth guard to them. The category READMEs still interleave open and retired entries in their `## Tasks`/member lists (~300 done-links total; `rendering/README.md` alone has ~164), which was deliberately deferred to keep the PROC-003 slice reviewable.
- The guard extension should treat a done-link inside a category README's open-task list as a finding while exempting entries under a heading matching history/retired semantics.
- Depends on retired `PROC-003`. Lower priority than `PROC-004`/`PROC-006`; pick up when touching a category README anyway or as a standalone mechanical sweep per category (one category per commit keeps diffs reviewable).
- Landed (2026-06-10) in two slices. Slice A (mechanical): all category READMEs split retired entries verbatim under `## Retired` headings, open entries cite retired tasks as plain code spans, emptied open lists carry explicit none-open lines, the workshop pack's completed execution record is re-headed as history, and `bugs/index.md` already conformed via its `Verified / Closed` heading. Slice B (guard + docs): `validate_category_indexes` in `check_task_state_links.py` scans `tasks/backlog/*/README.md` + `tasks/backlog/bugs/index.md` with heading-stack semantics (history keywords retired/history/closed/completed/resolved/verified/done; ATX headings require a space so PR refs like `#921` never pop the stack), the rendering DAG section is exempted explicitly with `<!-- state-link-guard: allow-done-links -->` per this task's allowance while its non-DAG sections were de-linked, and `docs/agent/task-format.md` step 4 + skill mirrors document the new convention. The throwaway done-link probe produced exactly one finding and was removed.
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Required changes
- [x] For each category README under `tasks/backlog/*/` (and `bugs/index.md`), split lists into open entries and a `## Retired` (or equivalent) history section, preserving every entry verbatim.
- [x] Extend `validate_state_only_indexes` in `tools/agents/check_task_state_links.py` to scan category READMEs, treating done-links outside history-marked headings as findings.
- [x] Keep the rendering DAG annotations intact — where a DAG line references retired prerequisites, the link may stay if the line lives under a history-marked heading or the DAG section is exempted explicitly.

## Tests
- [x] `python3 tools/agents/check_task_state_links.py --root . --strict` passes after the restructure.
- [x] Add a throwaway done-link to a category README open list, confirm the extended guard fails, then remove it.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs
- [x] Update the "Retiring a task" procedure note in `docs/agent/task-format.md` if the category-README convention changes (currently: category READMEs may keep retired entries).
- [x] Re-run the skill mirror sync if `task-format.md` changes.

## Acceptance criteria
- [x] No category README mixes open and retired entries in one list.
- [x] The state-only link guard covers category READMEs without false positives on history sections.

## Verification
```bash
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Dropping or rewording retired entries during the move.

# PROC-002 — Task ID uniqueness validation and allocation rule

## Goal
- Enforce task-ID uniqueness across `tasks/active|backlog|done` in strict CI mode, grandfather the existing collisions without renaming retired files, and document a deterministic ID allocation rule for agents opening new tasks.

## Non-goals
- No renaming of existing duplicate-ID files under `tasks/done/` — they are link targets across docs and reports, and the audit trail outweighs tidy IDs.
- No central ID registry file or reservation service; allocation stays convention-plus-validator.
- No changes to the task template's nine-section structure.

## Context
- Owner/layer: task tooling (`tools/agents/validate_tasks.py` or `check_task_policy.py`) and task-format docs. No engine code.
- Confirmed collisions in `tasks/done/` (2026-06-09): `BUG-021` (×2), `BUG-022` (×2), `HARDEN-065` (×3), `HARDEN-066` (×2), `HARDEN-067` (×2). Concurrent agents independently took the same "next" ID and no validator caught it.
- Task IDs are the join keys for the backlog dependency anchors, the retirement log, and audit reports; collisions corrode the dependency graph silently.
- `validate_tasks.py` already parses task IDs from the title line (`TASK_ID_RE`), so the uniqueness pass belongs there; `check_task_policy.py` already invokes it in strict mode from `ci-docs.yml`.
- Independent of `PROC-001`; should land before `PROC-003`/`PROC-004` open follow-up tasks so new IDs are protected.

## Required changes
- [x] Add a uniqueness pass to `tools/agents/validate_tasks.py`: collect the parsed task ID for every structured task file across `active/`, `backlog/`, and `done/`, and report a finding for each ID owned by more than one file.
- [x] Add a `GRANDFATHERED_DUPLICATE_IDS` constant in the validator listing exactly the seven known collisions (`BUG-021`, `BUG-022`, `HARDEN-065`, `HARDEN-066`, `HARDEN-067`), each entry commented with the colliding filenames and why they are frozen rather than renamed.
- [x] Treat a grandfathered ID as a finding only if a *new* (third-plus or newly created) file claims it — the allowlist freezes the current state, it does not exempt the prefix.
- [x] Document the allocation rule in `docs/agent/task-format.md`: before opening `<PREFIX>-<N>`, compute `N = max(existing <PREFIX>-* across tasks/active, tasks/backlog, tasks/done) + 1`, with the exact lookup one-liner (e.g. `ls tasks/*/ tasks/backlog/*/ | grep -oE '^<PREFIX>-[0-9]+' | sort -V | tail -1`) so agents do not skip it.

## Tests
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes on the current tree (grandfathered collisions produce no strict failure).
- [x] Create a throwaway duplicate (e.g. copy a backlog task to a second filename with the same ID), confirm strict mode fails naming both files, then remove it.
- [x] Create a throwaway file claiming a grandfathered ID, confirm strict mode fails, then remove it.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes end-to-end.

## Docs
- [x] `docs/agent/task-format.md` gains an "ID allocation" subsection with the rule and lookup command.
- [x] Re-run the skill mirror sync (`PROC-001` tooling if landed, else `resync_skills.sh`) so the `intrinsicengine-task-workflow` reference picks up the new subsection.

## Acceptance criteria
- [x] A PR introducing a duplicate task ID fails `ci-docs.yml` with a message naming both files.
- [x] The seven historical collisions remain untouched on disk and produce no strict finding.
- [x] `docs/agent/task-format.md` states the allocation rule and lookup command.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Renaming or editing any retired task file under `tasks/done/`.
- Adding new entries to the grandfathered allowlist for collisions created after this task lands.

## Completion

- Completed 2026-06-09 on branch `claude/agentic-workflow-analysis-kohifk`.
- Commit: the PROC-002 implementation commit on that branch (uniqueness pass
  in `validate_tasks.py`, five grandfathered IDs, ID-allocation rule in
  `docs/agent/task-format.md`).
- Note: an open-task collision suspected during planning (three `RORG-031-*`
  filenames) turned out to be benign — their title-line IDs are distinct
  (`RORG-031C`/`E`/`F`); uniqueness is keyed on the title-line ID.
- Review follow-up (PR #976, 2026-06-09): the grandfathered-duplicate
  comparison was strengthened from basenames to full `tasks/`-relative
  paths, so copying an allowed file into another lifecycle directory (same
  basename, different location) no longer slips past the uniqueness rule.

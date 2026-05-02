# Task 3 — Fix task validator to match documented task format

- Status: completed
- Owner: Claude (claude/next-active-task-PMyma)
- Branch / PR: `claude/next-active-task-PMyma`
- Next verification step: `python3 tools/agents/validate_tasks.py --root tasks --strict` (passes; 46 files validated, 0 findings).

`tools/agents/validate_tasks.py` now requires `Context` and `Forbidden changes` in `REQUIRED_SECTIONS`, matching `docs/agent/task-format.md`. Queue/index meta-files under `tasks/active/` (`task-NN-*.md`, `RENDERING-CLEANUP-TASK-PACK.md`) are excluded via `SKIP_PATTERNS`/`SKIP_FILENAMES` because they are queue ordering trackers, not canonical structured tasks. Four pre-existing `tasks/done/` files (`HARDEN-050-ci-policy-alignment.md`, `HARDEN-052-offline-dependency-cache-bootstrap.md`, `final-post-reorganization-hardening-audit.md`, `final-reorganization-audit.md`) received minimal `Context` / `Forbidden changes` sections to match the tightened policy. All three validators (`validate_tasks.py`, `check_task_policy.py --strict`, `check_doc_links.py --strict`) report zero findings.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Fix the task validator so it enforces the documented task format.

## Problem

`docs/agent/task-format.md` says new task files require:

- Goal
- Non-goals
- Context
- Required changes
- Tests
- Docs
- Acceptance criteria
- Verification
- Forbidden changes

But `tools/agents/validate_tasks.py` currently does not require Context or Forbidden changes.

## Required changes

1. Update `tools/agents/validate_tasks.py`:
   - Add `Context` to `REQUIRED_SECTIONS`.
   - Add `Forbidden changes` to `REQUIRED_SECTIONS`.
2. Run strict validation.
3. Fix any structured task files that now fail by adding missing sections.
   - Keep edits minimal and factual.
   - Do not rewrite unrelated task content.
4. Add or update a small validator test if the repo already has tests for `tools/agents` validators.
   - If there is no existing test harness for this script, do not create a large new framework; just keep the script change and validation output.

## Acceptance criteria

- `validate_tasks.py` enforces the same required section list as `docs/agent/task-format.md`.
- Existing structured task files pass strict validation.
- No task loses information.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No renderer changes.
- No build-system restructuring.
- No broad task rewrites beyond adding required missing sections.

This is important because the validator currently passes but is weaker than the documented workflow. The task-format doc requires these sections, while the validator does not enforce all of them.

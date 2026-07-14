# HARDEN-075 — Validate task-state links and stale status claims

## Goal
- Add a repository task-state consistency check that reports markdown links and
  prose claims that say a task is active, backlog, or done when the task file is
  actually missing or lives in a different lifecycle directory.

## Non-goals
- Do not promote, retire, reprioritize, or rewrite task files beyond the minimal
  stale-link/status corrections needed for the checker to pass.
- Do not replace `tools/agents/check_task_policy.py`; this task adds a
  cross-reference/state check that the structural policy checker does not
  currently cover.
- Do not make the checker infer nuanced roadmap status from arbitrary prose.
  Keep the first implementation narrow and deterministic.

## Context
- Owning subsystem/layer: agent workflow tooling under `tools/agents/`, task
  records under `tasks/`, and regression coverage under
  `tests/regression/tooling/`.
- The quality assessment found backlog prose that linked `GRAPHICS-077`,
  `GRAPHICS-078`, and `RUNTIME-092` as `tasks/active` files while
  `tasks/active/` contained only `README.md`.
- Existing validators confirm that task files are well-formed, but they do not
  prove that cross-links and lifecycle-state claims agree with the actual
  `tasks/backlog/`, `tasks/active/`, and `tasks/done/` tree.
- This task complements
  [`HARDEN-074`](HARDEN-074-doc-link-checker-inline-code-labels.md): link-target
  existence and task lifecycle consistency are separate signals.
- Related audit record:
  [`docs/reviews/2026-06-02-repository-quality-assessment.md`](../../docs/reviews/2026-06-02-repository-quality-assessment.md).

## Required changes
- [x] Add `tools/agents/check_task_state_links.py` or extend
  `tools/agents/check_task_policy.py` with an explicit task-state
  cross-reference pass. Prefer a separate script if that keeps diagnostics and
  tests simpler.
- [x] Build an index of task IDs to lifecycle location from `tasks/backlog/`,
  `tasks/active/`, and `tasks/done/`.
- [x] Detect relative markdown links under `tasks/` that target a task file in a
  lifecycle directory inconsistent with the linked task ID's actual location.
- [x] Detect deterministic status phrases in task/backlog docs, such as
  `active task`, `moved to active`, `done task`, or `backlog task`, when they
  occur near a task ID whose actual file location disagrees.
- [x] Report diagnostics with source path, line number, task ID, claimed state,
  actual state, and suggested target path.
- [x] Fix current stale task-state links and prose claims uncovered by the new
  checker.
- [x] Document the checker in `tools/agents/README.md`.
- [x] Wire the checker into `tools/ci/touched_scope.py` for changes under
  `tasks/` and relevant `docs/agent/` workflow files.

## Tests
- [x] Add Python regression tests under `tests/regression/tooling/` covering a
  task ID whose actual file is in `tasks/backlog/` but whose markdown link
  points at `tasks/active/`.
- [x] Add a regression test for stale lifecycle prose near a task ID.
- [x] Add a regression test proving historical links to `tasks/done/` remain
  valid when they explicitly describe completed work.
- [x] `python3 tools/agents/check_task_state_links.py --root . --strict` passes,
  or the equivalent `check_task_policy.py` strict mode passes if the check is
  implemented there.

## Docs
- [x] Update `tools/agents/README.md` with the new checker purpose and command.
- [x] Update `docs/agent/task-format.md` if task authors need a specific
  lifecycle-linking convention.
- [x] Update `docs/agent/prompt/prompt.md` if the default structural-check list
  should include the new command.

## Acceptance criteria
- [x] A stale link from backlog prose to a nonexistent `tasks/active/<ID>.md`
  file fails strict mode even when generic markdown link checking would pass or
  miss it.
- [x] A task ID linked to the wrong lifecycle directory reports the task's
  actual lifecycle location.
- [x] Current repository task-state links pass after targeted corrections.
- [x] `tools/ci/touched_scope.py` includes the check for task/workflow-doc
  changes.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckTaskStateLinks.py
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Hiding stale lifecycle claims by deleting task IDs instead of correcting the
  target state or wording.
- Expanding the checker into subjective roadmap interpretation.
- Failing historical `tasks/done/` links that clearly describe completed work.
- Combining this workflow hardening with unrelated task promotion or retirement.


## Completion
- Completed: 2026-06-02.
- Status: done.
- Commit reference: this commit (`HARDEN-075: check task lifecycle links`).

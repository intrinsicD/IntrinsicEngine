# HARDEN-076 — Enforce open task owners for layering allowlist rows

## Goal
- Extend `tools/repo/check_layering_allowlist_quality.py` so each
  `tools/repo/layering_allowlist.yaml` entry must reference an existing open
  task under `tasks/backlog/` or `tasks/active/`, not a missing task or a task
  already retired to `tasks/done/`.

## Non-goals
- Do not rebind allowlist rows in this task; that metadata migration is owned by
  [`HARDEN-069`](HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md).
- Do not add, delete, widen, or narrow any allowlisted layering edge.
- Do not change `tools/repo/check_layering.py` dependency-boundary rules.
- Do not migrate or delete legacy code.

## Context
- Owning subsystem/layer: repository layering tooling under `tools/repo/` and
  temporary migration-exception governance.
- `HARDEN-069` fixes the current data problem: 81 allowlist rows reference the
  completed `HARDEN-010` task. This task makes that problem mechanically
  difficult to reintroduce.
- `check_layering_allowlist_quality.py` currently validates required metadata,
  duplicate keys, and broad legacy wildcard bans, but it does not prove that the
  `task:` value points at an open removal owner.
- Per [`AGENTS.md`](../../../AGENTS.md) §13, temporary migration exceptions are
  valid only when documented in a current task with a specific removal owner.
- Related audit record:
  [`docs/reviews/2026-06-02-repository-quality-assessment.md`](../../../docs/reviews/2026-06-02-repository-quality-assessment.md).

## Required changes
- [ ] Implement this task after `HARDEN-069` has landed, or batch it after the
  allowlist metadata has already been rebound away from `HARDEN-010`.
- [ ] Extend `tools/repo/check_layering_allowlist_quality.py` to build a task ID
  index from `tasks/backlog/`, `tasks/active/`, and `tasks/done/`.
- [ ] In strict mode, fail when an allowlist row's `task:` value is missing,
  unknown, duplicated across lifecycle directories, or found only under
  `tasks/done/`.
- [ ] In warning mode, report the same findings without failing, matching the
  script's current warning/strict behavior.
- [ ] Include the allowlist row index or source line, `from`, `to`, `file_glob`,
  and invalid `task:` value in diagnostics.
- [ ] Add regression coverage under `tests/regression/tooling/` with temporary
  allowlist/task fixtures for open, done, missing, and duplicate task IDs.
- [ ] Update `tools/repo/README.md` to document that allowlist task owners must
  be open.

## Tests
- [ ] Add a Python regression test, for example
  `tests/regression/tooling/Test.CheckLayeringAllowlistQuality.py`.
- [ ] Test that an allowlist row pointing at `tasks/backlog/<ID>.md` passes.
- [ ] Test that an allowlist row pointing at `tasks/done/<ID>.md` fails strict
  mode.
- [ ] Test that a missing task ID fails strict mode.
- [ ] Test that warning mode reports findings but exits successfully.

## Docs
- [ ] Update `tools/repo/README.md` with the open-task-owner rule.
- [ ] If `docs/architecture/layering.md` describes temporary exceptions, add a
  short note that allowlist rows must point at open removal tasks.
- [ ] Link `HARDEN-069` and this task from any migration note updated while
  rebinding allowlist metadata.

## Acceptance criteria
- [ ] `python3 tools/repo/check_layering_allowlist_quality.py --root . --strict`
  fails on fixture rows that reference `tasks/done/` owners.
- [ ] The same command passes on the repository after `HARDEN-069` has rebound
  all current rows.
- [ ] Diagnostics identify the exact allowlist row and the task lifecycle
  mismatch.
- [ ] No allowlist edge tuple `(from, to, file_glob)` changes in this task.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckLayeringAllowlistQuality.py
python3 tools/repo/check_layering_allowlist_quality.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Implementing this before the repository data can pass strict mode unless the
  task also includes the already-approved `HARDEN-069` metadata rebinding.
- Disabling strict failures for done or missing task owners.
- Changing layering policy to reduce the number of reported allowlisted
  violations instead of validating owner metadata.
- Deleting legacy code or migration docs as part of this tooling task.

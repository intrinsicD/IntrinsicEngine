---
id: BUG-225
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of a task-tooling inconsistency found while retiring GEOM-024; no claim changes.
contract_schema: 1
contracts: []
contract_review: Task-link maintenance under the existing task workflow; no engine or integration contract.
---
# BUG-225 — Archived tasks cannot follow links to tasks that retire later

## Goal
- Resolve the rule conflict so retiring a backlog task never forces a broken link or an edit of frozen history.

## Context
Found on 2026-09-27 when GEOM-024 moved from `tasks/backlog/geometry/` to
`tasks/done/`. The archived `GEOM-020` links to GEOM-024's backlog path. The archive
README forbids editing archived files, and `validate_tasks.py` also treats a changed
archived file as outside the contract baseline, yet `check_doc_links.py` now reports
the link as broken (warning mode, non-fatal). Any archived task that links to a
still-open task hits the same conflict when that task retires.

### 2026-10-01 verification observation

The strict link check on the method-review session still fails on the two
GEOM-020 references. It also reports stale retired-task paths in the methods,
rendering, and runtime backlog README files (METHOD-056, GRAPHICS-148,
RUNTIME-290). These files were unchanged by this session. The complete
[strict output](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-225/2026-10-01-doc-links.log) is retained;
the default warning-mode exit is not a passing strict gate. No archive or
checker policy was changed.

## Resolution — 2026-10-01

Archived task links may change only their directory when the same task moves.
`validate_tasks.py` compares against immutable Git baseline bytes: task filename,
fragment, link labels, and all other text stay unchanged; the new target must
exist inside a task lifecycle directory. Regression tests reject content changes,
wrong tasks, missing destinations, absolute/noncanonical paths, and destinations outside the task tree.
The GEOM-020 links now point to GEOM-024 in `done/`. Retired entries were removed
from open indexes and completed agent-control references placed under history
headings. The canonical contract and archive README state this narrow exception.

Verification: 31 task-validator tests, 3 doc-link tests, 7 task-state-link tests,
strict doc links, strict task policy and strict task-state links.
The task-state regression initially had a
[stale CI-script expectation](https://github.com/intrinsicD/IntrinsicEngine/blob/599f02ff1b34afb96ad34f8d3c3d24f6d2ba6b02/tasks/evidence/BUG-225/2026-10-01-stale-ci-policy-expectation.log);
it now requires the two already-enabled compiler-hazard/MCP regression scripts
as well, preserving the exact-list check. No broken links are ignored.
Implementation is verified in the working tree; retirement awaits a commit reference.

## Acceptance criteria
- [x] Pick one rule and apply it in the tools and `tasks/archive/README.md`: e.g. allow path-only link rewrites in archived files (with the contract baseline ignoring link-only diffs), or have `check_doc_links.py` resolve task links by ID across `backlog/`, `done/` and `archive/`.
- [x] `check_doc_links.py` is clean again, including `tasks/archive/GEOM-020-sparse-direct-factorization-seam.md`.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Resolution — 2026-10-24

Retirement preparation only; no implementation change.

- Implementation: `2688e7480` on `main`. `tools/agents/validate_tasks.py` and
  `tests/regression/tooling/Test.ValidateTasks.py` are unchanged since. `ci-docs`
  runs strict doc links, `Test.ValidateTasks.py` and `Test.CheckTaskStateLinks.py`.
  It did not run on `main` between 2026-09-15 and 2026-10-08; its first green `main`
  run after the commit is at `b4e0ca630` (2026-10-08). It is also green at `599f02ff1`.
- Checked on 2026-10-24 at `599f02ff1`: `Test.ValidateTasks.py` passes 31 tests and
  `Test.CheckTaskStateLinks.py` passes 7. Strict doc links report no broken links.
  With only the validator restored to `2688e7480^` there are 6 failures, including
  `test_archived_task_link_may_follow_retired_target`. Real-repository discovery
  also fails, because the old validator rejects the repaired GEOM-020 archive file.
- Durable record: the path-only exception in `docs/agent/contract.md` (and its
  generated skill mirror), `tasks/archive/README.md` and `tools/agents/README.md`.
  The executable safeguard is the archived-link tests in `Test.ValidateTasks.py`.
  The facts from both logs are already in the text above: the five broken links
  before the fix, and the exact-list expectation that lacked
  `Test.CheckCompilerHazards.py` and `Test.McpBridge.py`. There is no reusable
  lesson beyond the contract rule.
- The evidence links above now use exact revision `599f02ff1`; those objects are
  byte-identical to `2688e7480`. Prepared deletion: the 2 tracked files under
  `tasks/evidence/BUG-225/`.

## Completion
Completed 2026-10-24. Commit: `2688e7480`.
Reviewer decision 2026-10-24: independent Claude review approved at `800f1c371` (implementation,
summaries, link repairs and deletion list, checked against the raw evidence before deletion):
AC1 "met", AC2 "met". The reviewer re-ran `Test.ValidateTasks.py` (31 pass) and strict doc
links (4649 links, none broken; GEOM-020 lines 15 and 39 resolve to `../done/GEOM-024-...`).
Maturity: Operational. The validator and strict doc-link check run on the real repository in
`ci-docs` (green at `599f02ff1`, run 39957196726); the archived-link cases in
`Test.ValidateTasks.py` pin the rule. The 2 tracked files under `tasks/evidence/BUG-225/` were
deleted at retirement; the links above resolve at Git revision `599f02ff1`.

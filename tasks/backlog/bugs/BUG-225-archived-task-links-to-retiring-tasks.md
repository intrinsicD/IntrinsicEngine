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
[strict output](../../evidence/BUG-225/2026-10-01-doc-links.log) is retained;
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
[stale CI-script expectation](../../evidence/BUG-225/2026-10-01-stale-ci-policy-expectation.log);
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

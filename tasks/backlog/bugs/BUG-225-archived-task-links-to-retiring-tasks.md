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

## Acceptance criteria
- [ ] Pick one rule and apply it in the tools and `tasks/archive/README.md`: e.g. allow path-only link rewrites in archived files (with the contract baseline ignoring link-only diffs), or have `check_doc_links.py` resolve task links by ID across `backlog/`, `done/` and `archive/`.
- [ ] `check_doc_links.py` is clean again, including `tasks/archive/GEOM-020-sparse-direct-factorization-seam.md`.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

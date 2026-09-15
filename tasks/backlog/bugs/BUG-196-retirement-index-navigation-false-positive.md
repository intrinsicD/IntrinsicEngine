---
id: BUG-196
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation]
---
# BUG-196 — Retirement navigation is misclassified as task membership

## Goal
Allow brief links to the canonical done/archive directory indexes in task
entrypoints while keeping retired task entries out of live task lists.

## Non-goals
No engine/build change, policy weakening, broad task-link exception, history
sections in READMEs, or unrelated backlog rewrite. No performance claim.

## Context

Observed during the user-authorized overnight cleanup with Claude on
2026-09-15. DOCS-007 retains a usable current index pending this focused fix.

### Evidence and diagnosis
DOCS-007's strict structural check rejected category README links to
`tasks/done/README.md` and `tasks/archive/README.md` as retired task entries:
`tasks/evidence/DOCS-007/commands/structural.json` and its stdout log retain
both findings. `validate_category_indexes` in
`tools/agents/check_task_state_links.py` treats every target under done/archive
as task membership except RETIREMENT-LOG.md. `validate_state_only_indexes`
uses the same broad predicate. Directory roots are also misclassified.

The current source-documentation policy allows brief links to authoritative
history owners, but rejects README history sections. A canonical index/directory
link is navigation, not a retired-task entry. Actual retired task links must
remain rejected in live lists. DOCS-007 temporarily uses the already-permitted
retirement-log link and plain index paths so both existing gates pass.

## Scope and plan
After DOCS-007 is retired/sealed and its writer stops, inspect the existing
checker and `tests/regression/tooling/Test.CheckTaskStateLinks.py` with Claude.
Reuse one narrow navigation predicate in the two present checks if appropriate:
exact done/archive roots and their README indexes, plus existing retirement-log
behavior. No exemption based on arbitrary filenames/subdirectories or link text.
Test allowed canonical navigation and rejection of actual retired task records
in both top-level live indexes and category READMEs. Preserve fence, relative
path, heading and existing task-state behavior. Restore clickable done/archive
index links in the short runtime README and run both validators together.

## Required changes
- [ ] Distinguish canonical retirement navigation from actual task membership.
- [ ] Cover both validator paths and preserve real retired-task rejection.
- [ ] Restore concise clickable runtime index navigation without history sections.

## Tests
- [ ] Reproduce current false positive in focused tooling tests before the fix.
- [ ] Focused tooling tests, strict task-state/doc/task checks and scoped README audit pass.

## Docs
- [ ] Record Claude review, failure/fix evidence, completion and exact source seal.

## Acceptance criteria
- [ ] Canonical index navigation passes while actual retired task entries still fail in live lists.
- [ ] Current README navigation satisfies both source-documentation and task-state contracts.
- [ ] Reviewed fix is locally committed, retired and sealed with no engine or policy change.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckTaskStateLinks.py
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path tasks/backlog/runtime/README.md --summary
python3 tools/agents/workflow_evidence.py validate --root .
```
Use the test file's actual invocation if inspection requires one. No C++ or GPU
rebuild: only Python validation and current README navigation change. The
corrected plan below was reviewed with Claude before implementation; this remains
prepared work, not a completed fix.

## Forbidden changes
- Allowing arbitrary retired task links in live lists to make the gate green.
- Adding compatibility/history sections or bypass markers to the runtime README.
- Editing existing retired task records or unrelated source files.

## Settled plan — Claude review, 2026-09-15
Claude approved one module-level `is_retirement_navigation(target, tasks_root)`
predicate beside the existing retirement-log constant, used by both current
index checks. Preserve the existing name-based RETIREMENT_LOG_NAME exemption.
Add only the exact done/archive roots and their direct README.md paths, using
the same resolved-root assumptions as the existing callers. No change to
`validate_link_states` is needed: navigation paths do not carry task IDs.

Reuse the existing fixture helpers. Add positive cases for both roots with and
without trailing slash, direct README indexes and README fragments in top-level
live and category indexes. Keep actual retired-task links, nested README paths,
and misleading link text rejected; a fragment on an actual task must still fail.
Do not add a heading containing `verified` or other history terms, since the
existing history-heading matcher would exempt that section. Restore navigation
inside the current Related queues and documentation paragraph. The optional
methods-queue link may be omitted; no further README restructuring is needed.

Root accepts these refinements within the user-authorized cleanup. Baseline
reproduction must fail for the intended navigation finding; final tests and both
existing validators must pass before retirement. Use existing Python tooling,
with no C++ rebuild or new package dependency. CLI plan review is retained under
`/tmp/intrinsic-overnight-20260915/docs007/claude-plan-bug196.txt`.

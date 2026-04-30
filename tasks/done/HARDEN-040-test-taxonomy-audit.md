# HARDEN-040 — Audit remaining non-taxonomic test directories

## Goal
Produce a factual audit of remaining non-taxonomic test directories and map each directory to an explicit follow-up disposition for taxonomy hardening.

## Non-goals
- Do not migrate, delete, or rewrite legacy test sources.
- Do not change runtime or graphics test behavior.
- Do not modify `src/legacy/`.

## Context
Owned by tests/docs under post-reorganization hardening. This task is documentation-first and prepares HARDEN-041/HARDEN-042 by inventorying the old wrapper test directories that are currently excluded from active CTest registration.

## Required changes
- Create `docs/reports/test-taxonomy-audit-2026-04-29.md` with:
  - Current canonical taxonomy roots in `tests/`.
  - Remaining non-taxonomic directories and file inventories.
  - Current registration status and rationale.
  - Concrete follow-up action mapping to HARDEN-041 and HARDEN-042.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` status/evidence for HARDEN-040.

## Tests
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Add the new audit report in `docs/reports/`.
- Synchronize hardening tracker status and evidence log.

## Acceptance criteria
- [x] Audit report exists with directory-by-directory disposition.
- [x] Non-taxonomic directories are explicitly linked to follow-up hardening tasks.
- [x] Hardening tracker reflects HARDEN-040 progress and links the report.
- [x] Strict task/doc validation passes.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No source-level migration of old test wrappers in this task.
- No C++ test semantic edits.
- No silent changes to CTest registration policy.

## Completion metadata
- Completion date: 2026-04-29.
- Commit reference: pending current workspace/PR.
- Follow-up: HARDEN-041 performed the mechanical source relocation; HARDEN-042 removed obsolete wrapper stubs.


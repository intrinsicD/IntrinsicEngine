# Migration Documentation

This section tracks **temporary** repository transition work from the dual-tree state (`src/` + `src_new/`) to the target canonical layout.

## Scope and status

- These docs are migration-scoped and may become obsolete when reorganization is complete.
- Canonical long-lived architecture docs remain under `docs/architecture/`.
- Temporary exceptions must also be recorded in a current task under `tasks/active/` with a removal task ID.

## Migration documents

- [Current repository inventory](current-repo-inventory.md) — factual snapshot baseline before structural moves.
- [Target repository layout](target-repo-layout.md) — agreed final-state directory contract.
- [Legacy retirement plan](legacy-retirement.md) — policy and exit criteria for `src/legacy/`.
- [Source-tree reorganization](source-tree-reorganization.md) — phased move strategy and safeguards.
- [Source-tree move plan](source-tree-move-plan.md) — exact Phase 9 move table, build/path update scope, and rollback procedure.
- [Source-tree reorganization transition status](active-status.md) — status board for promotion from migration layouts into final `src/` roots.
- [src_new reference audit](src-new-reference-audit.md) — HARDEN-030 classification of remaining `src_new` / `src-new` references.
- `docs/migration/src_new_module_inventory.md` — generated migration inventory (kept in current path until generator/path migration task).

## What is explicitly temporary

The following are migration-only artifacts and should be removed or archived when the final source layout is complete:

- Active references to `src_new/` as a promotion source.
- Transition exception lists for layering or path compatibility wrappers.
- Move-phase checklists once they are reflected in final architecture docs.

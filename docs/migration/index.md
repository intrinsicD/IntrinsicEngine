# Migration Documentation

This section tracks **temporary** repository transition work toward the target canonical `src/` layout.

## Scope and status

- These docs are migration-scoped and may become obsolete when reorganization is complete.
- Canonical long-lived architecture docs remain under `docs/architecture/`.
- Temporary exceptions must also be recorded in a current task under `tasks/active/` with a removal task ID.

## Migration documents

- [Target repository layout](target-repo-layout.md) — agreed final-state directory contract.
- [Legacy retirement plan](legacy-retirement.md) — historical retirement evidence for the deleted `src/legacy/` tree.
- [Non-legacy parity matrix](nonlegacy-parity-matrix.md) — promoted `src/` subsystem parity state after legacy retirement.
- [Source-tree reorganization](source-tree-reorganization.md) — phased move strategy and safeguards.
- [Source-tree reorganization transition status](active-status.md) — status board for promotion into final `src/` roots.

## What is explicitly temporary

The following are migration-only artifacts and should be removed or archived when the final source layout is complete:

- Transition exception lists for layering or path compatibility wrappers.
- Move-phase checklists once they are reflected in final architecture docs.

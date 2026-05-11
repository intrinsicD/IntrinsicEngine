# RORG-032 — Non-legacy parity matrix

## Goal

Create a factual parity matrix for promoted `src/` subsystems excluding
`src/legacy/`.

## Non-goals

- No semantic C++ refactors.
- No edits under `src/legacy/`.
- No declaration that legacy retirement is complete.
- No new CI workflow unless a simple deterministic check is necessary.

## Context

Owned by migration documentation. The matrix uses
`docs/api/generated/module_inventory.md` plus inspection of canonical promoted
roots under `src/{core,assets,ecs,geometry,graphics,platform,runtime,app}`.

## Required changes

- [x] Add `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Link the matrix from migration documentation indexes.
- [x] Reference legacy module names only as replacement targets.

## Tests

- [x] Run module inventory freshness check.
- [x] Run strict markdown link validation.

## Docs

- [x] Update `docs/migration/index.md`.
- [x] Update `docs/index.md`.

## Acceptance criteria

- [x] The matrix lists current modules, likely legacy subsystem replaced, missing or
  unproven behavior, required tests, and retirement blocker for each promoted
  subsystem.
- [x] No `src/legacy/` files are modified.
- [x] No semantic code changes are made.

## Verification

```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/docs/check_doc_links.py --root . --strict
```

## Completion

- Completed: 2026-04-30.
- Commit: d3d2ece.

## Forbidden changes

- Do not modify `src/legacy/`.
- Do not regenerate inventory unless module declarations changed or the check
  reports staleness.
- Do not introduce source-level behavior changes.


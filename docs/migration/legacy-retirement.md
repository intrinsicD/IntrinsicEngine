# Legacy Retirement Plan (`src/legacy/`)

## Purpose

`src/legacy/` is a temporary containment area for historical subsystems that have not yet been promoted into canonical final roots (`src/core`, `src/geometry`, `src/graphics/*`, etc.).

## Policy

- New feature work should target canonical layers, not `src/legacy/`, unless needed for compatibility.
- Any temporary cross-layer exception inside `src/legacy/` must be tracked in `tasks/active/0000-repo-reorganization-tracker.md`.
- Promotion work from `src/legacy/` must keep mechanical path moves separate from semantic refactors.

## Exit criteria

`src/legacy/` can be considered retired only when all are true:

1. Canonical implementations exist and build under final roots.
2. CI no longer depends on legacy include paths.
3. Layering checks pass with no undocumented legacy exceptions.
4. Migration docs referencing active legacy shims are closed or archived.

## Cleanup expectations

When a legacy area is retired:

- Remove now-unused compatibility wrappers.
- Update architecture docs and inventory outputs.
- Mark related migration tasks as done with commit/PR references.

# Rendering Architecture Refactor — Plan (Archive)

This file is the archival index for the completed three-pass rendering refactor in the legacy `src/` tree.

## Status

- Refactor status: complete (legacy `src/` tree).
- Completion window: 2026-03 (Phases 1-9 merged).
- Historical implementation details remain in Git history.

## Active forward-looking planning

The next architectural frontier is the **`src_new/` reimplementation** (see `CLAUDE.md` → "Active Effort: `src_new/` Reimplementation"). Forward-looking design lives in:

- `docs/architecture/src_new-rendering-architecture.md` — target design for `src_new/Graphics`.
- `docs/architecture/src_new_module_inventory.md` — live module inventory for `src_new`.
- Per-subsystem `src_new/<Subsystem>/README.md` files.

## Canonical Documents (legacy `src/` tree)

- `docs/architecture/rendering-three-pass.md` — canonical technical architecture spec for the legacy renderer (pass contracts, data contracts, invariants).
- `docs/architecture/runtime-subsystem-boundaries.md` — legacy runtime ownership map, dependency directions, and lifecycle order.
- `tasks/backlog/legacy-todo.md` — active near-term execution queue.
- `docs/roadmap.md` — medium/long-horizon sequencing.

## Notes

- Keep `docs/migration/archive/plan.md` intentionally small to avoid duplicating live architecture spec and active backlog content.
- Do not add new implementation backlog items here; use `tasks/backlog/legacy-todo.md`.

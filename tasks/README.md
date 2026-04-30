# Tasks System

This directory is the canonical work-tracking system for IntrinsicEngine.

## Status flow

Tasks move through this lifecycle:

1. `tasks/backlog/` — planned but not yet active work.
2. `tasks/active/` — currently in progress or currently blocked work.
3. `tasks/done/` — completed work with completion metadata.

Do not keep long-lived planning checklists in root-level `tasks/backlog/legacy-todo.md` files once a task has been migrated into this structure.

## Task ID prefixes

Use stable task prefixes to reflect ownership and review routing:

- `RORG-` — repository reorganization and migration.
- `ARCH-` — architecture and layering decisions.
- `GRAPHICS-` — renderer, frame graph, and RHI work.
- `GEOM-` — geometry processing and mesh pipeline work.
- `METHOD-` — paper/method implementation workflow.
- `BENCH-` — benchmark manifests, runners, and baselines.
- `CI-` — workflow and automation changes.
- `HARDEN-` — post-reorganization hardening, verification, and cleanup.

## Active trackers

None. `tasks/active/` should contain only currently in-progress or blocked task files.

Recently archived trackers:

- [RORG-0000 — Repository Reorganization Tracker](done/0000-repo-reorganization-tracker.md)
- [HARDEN-001 — Post-Reorganization Hardening Tracker](done/0001-post-reorganization-hardening-tracker.md)

## Required task shape

All new task files should be based on templates in `tasks/templates/` and include:

- Goal
- Non-goals
- Context
- Required changes
- Tests
- Docs
- Acceptance criteria
- Verification
- Forbidden changes

## Conventions

- Keep each task small and scoped whenever possible.
- Do not mix mechanical file moves with semantic refactors.
- Keep task descriptions factual and testable.

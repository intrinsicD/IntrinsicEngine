---
id: RORG-031A
theme: F
depends_on: []
---
# RORG-031A — Architecture foundation backlog seed

## Goal
- Convert the architecture-oriented items from the legacy living backlog into a structured task file that can be executed independently.

## Non-goals
- Moving source files.
- Changing runtime or renderer behavior.

## Context
- The previous `tasks/backlog/legacy-todo.md` document mixed architecture policy, implementation detail, and planning in one large file.
- Reorganization requires small, reviewable tasks with explicit acceptance and verification.
- Seed complete (2026-06-10): the architecture backlog is fully structured under `tasks/backlog/architecture/` (the LEGACY-001..012 retirement series, `HARDEN-078`, `INFRA-001`, and the category README with explicit gates), architecture governance tooling exists and runs in CI (`tools/repo/check_layering.py`, `tools/docs/check_docs_sync.py`, `tools/repo/generate_module_inventory.py` per `AGENTS.md` section 10), and migration/CI dependencies are recorded as consumer-grep gates and front-matter `depends_on` edges (e.g. `LEGACY-012` gated on `LEGACY-011`). The seed's job — replacing the legacy living backlog with structured, independently executable tasks — is done.
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Required changes
- [x] Track canonical architecture-doc normalization and taxonomy completion tasks under `tasks/backlog/architecture/`.
- [x] Track layering-checker, docs-sync-checker, and module-inventory tasks as explicit architecture governance work.
- [x] Record dependencies on migration and CI tasks where relevant.

## Tests
- [x] Ensure task file structure follows repository task template headings.

## Docs
- [x] Keep references aligned with `AGENTS.md` and `docs/architecture/index.md`.

## Acceptance criteria
- [x] Architecture backlog is represented by at least one structured task file under `tasks/backlog/architecture/`.
- [x] This task file includes all required sections (Goal, Non-goals, Context, Required changes, Tests, Docs, Acceptance criteria, Verification, Forbidden changes).

## Verification
```bash
test -f tasks/backlog/architecture/RORG-031A-architecture-foundation.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

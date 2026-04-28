# RORG-031A — Architecture foundation backlog seed

## Goal
- Convert the architecture-oriented items from the legacy living backlog into a structured task file that can be executed independently.

## Non-goals
- Moving source files.
- Changing runtime or renderer behavior.

## Context
- The previous `tasks/backlog/legacy-todo.md` document mixed architecture policy, implementation detail, and planning in one large file.
- Reorganization requires small, reviewable tasks with explicit acceptance and verification.

## Required changes
- Track canonical architecture-doc normalization and taxonomy completion tasks under `tasks/backlog/architecture/`.
- Track layering-checker, docs-sync-checker, and module-inventory tasks as explicit architecture governance work.
- Record dependencies on migration and CI tasks where relevant.

## Tests
- Ensure task file structure follows repository task template headings.

## Docs
- Keep references aligned with `AGENTS.md` and `docs/architecture/index.md`.

## Acceptance criteria
- Architecture backlog is represented by at least one structured task file under `tasks/backlog/architecture/`.
- This task file includes all required sections (Goal, Non-goals, Context, Required changes, Tests, Docs, Acceptance criteria, Verification, Forbidden changes).

## Verification
```bash
test -f tasks/backlog/architecture/RORG-031-architecture-foundation.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

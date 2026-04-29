# RORG-031D — src_new parity backlog seed

## Goal
- Break out `src_new` parity milestones into a structured task artifact with explicit scope.

## Non-goals
- Performing source-tree moves in this task.
- Declaring parity complete without objective acceptance checks.

## Context
- `src_new` subsystem parity milestones (Core, Assets, ECS, Platform, Graphics, Runtime, App) are currently tracked in legacy prose.

## Required changes
- Track parity milestones as independently executable work with clear ownership boundaries.
- Preserve the migration contract that module names remain stable during mechanical moves.
- Keep legacy retirement as dedicated follow-up tasks.

## Tests
- Ensure task structure compliance.

## Docs
- Maintain cross-links with migration docs and module inventory docs.

## Acceptance criteria
- `src_new` parity backlog is represented by a structured task file under `tasks/backlog/src-new/`.

## Verification
```bash
test -f tasks/backlog/src-new/RORG-031-src-new-parity.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

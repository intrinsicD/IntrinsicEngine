# RORG-031C — Runtime composition backlog seed

## Goal
- Capture runtime composition-root and lifecycle backlog work as a structured task.

## Non-goals
- Runtime API redesign in this task.
- Subsystem behavior changes.

## Context
- Runtime ownership and lifecycle gates are critical to the reorganization but were previously embedded in a broad narrative backlog.

## Required changes
- Track composition-root ownership (`begin_frame`, extraction, prepare, execute, end) as explicit runtime backlog work.
- Track shutdown determinism and subsystem wiring responsibilities under runtime ownership.

## Tests
- Ensure the task file uses repository task template sections.

## Docs
- Keep runtime architecture docs synchronized when this backlog item is executed.

## Acceptance criteria
- Runtime backlog exists as a structured task file under `tasks/backlog/runtime/`.

## Verification
```bash
test -f tasks/backlog/runtime/RORG-031-runtime-composition.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

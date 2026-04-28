# RORG-031E — Geometry and method-readiness backlog seed

## Goal
- Capture geometry-focused backlog and method-readiness work in structured task form.

## Non-goals
- Implementing new geometry algorithms.
- Changing geometry ownership boundaries in this task.

## Context
- Geometry remains a core subsystem target for migration and future methods/paper integration.

## Required changes
- Track geometry subsystem migration and test ownership tasks.
- Track alignment with future `methods/geometry` package requirements.
- Preserve architecture rule: canonical geometry implementation exits legacy location when promotion tasks complete.

## Tests
- Ensure this file follows task template structure.

## Docs
- Maintain alignment with `docs/architecture/geometry.md` and method workflow docs.

## Acceptance criteria
- Geometry backlog exists as a structured task file under `tasks/backlog/geometry/`.

## Verification
```bash
test -f tasks/backlog/geometry/RORG-031-geometry-method-readiness.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

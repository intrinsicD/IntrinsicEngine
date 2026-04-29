# RORG-031B — Rendering pipeline backlog seed

## Goal
- Extract rendering and frame-pipeline work from the legacy backlog into a dedicated structured task for phased execution.

## Non-goals
- Implementing new rendering features.
- Rewriting existing frame-graph passes in this task.

## Context
- Rendering pipeline hardening and GPU-driven submission work currently live inside a large narrative backlog.
- The migration plan expects isolated tasks with explicit verification.

## Required changes
- Track frame-pipeline hardening work (render prep ownership, submission ownership, frame-context ownership, task-graph/barrier hardening).
- Track dependencies between prep/submission/lifetime work and parallelization steps.
- Keep scope aligned to architecture hardening, not feature expansion.

## Tests
- Ensure this task file is parseable by the task template structure.

## Docs
- Maintain references to `docs/architecture/rendering-target-architecture.md` and related runtime/frame-loop docs while they remain active.

## Acceptance criteria
- Rendering backlog is represented by a structured task file under `tasks/backlog/rendering/` with required sections.

## Verification
```bash
test -f tasks/backlog/rendering/RORG-031-rendering-pipeline.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

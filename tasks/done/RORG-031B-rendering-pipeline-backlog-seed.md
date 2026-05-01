# RORG-031B — Rendering pipeline backlog seed

## Status

Superseded historical seed. Retired from `tasks/backlog/rendering/` and archived
here so the rendering backlog cannot be confused with the canonical
implementation plan.

- Completed: 2026-05-01.
- Commit: pending (lands with this task).
- PR: see branch `claude/next-active-task-CaTBY`.
- Superseded by: `tasks/backlog/rendering/GRAPHICS-001-rendering-parity-inventory.md`
  (canonical rendering backlog index) and the `GRAPHICS-002` through `GRAPHICS-021`
  task chain enumerated by `GRAPHICS-001`.
- Follow-up: workflow/cleanup tracking continues under
  `tasks/backlog/rendering/GRAPHICS-021-rendering-backlog-workflow-cleanup.md`.

This seed remains here only as a historical record of how the rendering
backlog was first extracted from the legacy living TODO; it is not an
executable plan.

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
test -f tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

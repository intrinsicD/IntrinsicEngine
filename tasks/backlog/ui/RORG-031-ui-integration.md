# RORG-031F — UI integration backlog seed

## Goal
- Extract editor/UI integration backlog items into a structured task file.

## Non-goals
- Implementing UI features.
- Refactoring panel systems in this task.

## Context
- Legacy backlog includes UI-facing requirements (view settings, inspector/undo-redo integration, operator accessibility) that need explicit task tracking.

## Required changes
- Track UI integration seams tied to runtime/renderer composition.
- Track undo/redo wiring and panel ownership as explicit follow-on tasks.

## Tests
- Ensure task structure compliance.

## Docs
- Keep UI-related architecture and migration docs synchronized when execution begins.

## Acceptance criteria
- UI backlog exists as a structured task file under `tasks/backlog/ui/`.

## Verification
```bash
test -f tasks/backlog/ui/RORG-031-ui-integration.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

# Module Rules

This document captures module/path stability expectations during reorganization.

## Rules

- Mechanical directory moves must preserve module names unless a task explicitly changes them.
- Mechanical moves and semantic refactors must not be mixed in one PR.
- New dependency edges must follow [layering.md](layering.md) and be documented.
- Transitional exceptions must be tracked in a current task under `tasks/active/` with a removal task ID.

## Review expectations

- Each PR should map to one migration task unless explicitly batched.
- CMake/docs/tests/scripts references must be updated alongside path moves.

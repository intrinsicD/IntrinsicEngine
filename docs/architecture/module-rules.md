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

## Shared standard declarations

`Extrinsic.Core.Std` compiles standard-library declarations separately for the
renderer and workspace snapshot interfaces. Its using-declarations introduce no
replacement types or engine dependency. These two measured consumers import it
without re-exporting helper names. A new consumer must justify its own producer,
importer and invalidation costs; this is not a blanket prelude for engine modules.

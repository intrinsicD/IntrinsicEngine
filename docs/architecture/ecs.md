# ECS Architecture

`ecs` owns entity/component data modeling and deterministic command/application semantics.

## Responsibilities

- Entity/component storage and mutation APIs.
- Scheduling-safe command/event application boundaries.
- Snapshot/export seams for rendering and runtime wiring.

## Dependencies

- Allowed: `core` and geometry handles/types only when explicitly justified.
- Disallowed: direct dependency on graphics/runtime/app internals.

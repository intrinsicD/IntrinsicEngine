# Frame Graph Architecture

The frame graph models render passes and resource dependencies per frame.

## Responsibilities

- Declare pass/resource DAG for graphics execution.
- Track transient resource lifetimes and scheduling constraints.
- Provide renderer-facing orchestration independent of gameplay ownership.

## Boundaries

- Frame graph lives under graphics architecture.
- Inputs are render-ready snapshots/views, not live ECS ownership state.

## Related references

- Graphics subsystem context: [graphics.md](graphics.md).

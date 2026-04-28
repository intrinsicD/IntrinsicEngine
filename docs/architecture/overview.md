# Architecture Overview

IntrinsicEngine uses a layered architecture optimized for modularity, testability, and migration safety.

## Layer map

- `core`: foundational primitives, utilities, and contracts.
- `geometry`: geometry-processing and mesh/domain algorithms built on `core`.
- `assets`: asset identity, metadata, and loading contracts built on `core`.
- `ecs`: entity/component systems built on `core` (and geometry handles only when explicitly justified).
- `graphics`: rendering stack split into `rhi`, backend(s), frame graph, and renderer.
- `runtime`: composition root that wires lower layers.
- `app`: top-level application/sandbox entrypoints that depend on `runtime`.
- `legacy`: temporary migration area with tracked exceptions only.

## Contract references

- Dependency and ownership rules: [layering.md](layering.md).
- Module/import constraints: [module-rules.md](module-rules.md).
- Execution graph ownership: [task-graphs.md](task-graphs.md) and [frame-graph.md](frame-graph.md).

This overview is normative; legacy planning docs remain in this folder for background and migration tracing.

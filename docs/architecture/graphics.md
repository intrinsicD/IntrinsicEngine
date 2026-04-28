# Graphics Architecture

Graphics is organized into explicit sublayers:

- `graphics/rhi`: low-level rendering hardware abstraction.
- `graphics/vulkan`: Vulkan backend implementation.
- `graphics/framegraph`: transient render dependency graph and scheduling.
- `graphics/renderer`: high-level render orchestration using snapshots/views.

## Rules

- Graphics consumes immutable/snapshot data from higher-level systems.
- Graphics must not depend on live ECS ownership structures.
- Backend code depends on RHI + allowed platform abstractions only.

## Related references

- Frame graph details: [frame-graph.md](frame-graph.md).
- Historical migration docs: `src_new-rendering-architecture.md`, `gpu-driven-modular-rendering-pipeline-plan.md`.

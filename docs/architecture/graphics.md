# Graphics Architecture

Graphics is organized into explicit sublayers:

- `graphics/rhi`: low-level rendering hardware abstraction.
- `graphics/vulkan`: Vulkan backend implementation.
- `graphics/framegraph`: transient render dependency graph and scheduling.
- `graphics/renderer`: high-level render orchestration using snapshots/views.

## Rules

- Graphics consumes immutable/snapshot data from higher-level systems.
- Graphics must not depend on live ECS ownership structures.
- Graphics-owned GPU handles, slots, leases, and backend resource IDs must not be stored in canonical `src/ecs` components.
- Runtime owns ECS-to-graphics extraction and any sidecar/cache mappings from ECS entities, asset IDs, or geometry source handles to graphics GPU handles.
- Runtime extraction is implemented by `Extrinsic.Runtime.RenderExtraction`, which queries live ECS, maintains entity-to-graphics sidecars outside canonical ECS components, and submits `RuntimeRenderSnapshotBatch` records through `IRenderer::SubmitRuntimeSnapshots()`.
- Backend code depends on RHI + allowed platform abstractions only.

## GPU scene ownership

- Runtime composes the CPU render scene from ECS, assets, geometry, transforms, materials, lights, selection, camera, and debug inputs.
- Graphics receives immutable `RenderWorld` / `RenderFrameInput` snapshots plus runtime-submitted transform/light/visualization record batches and uploads them into graphics-owned GPU scene buffers.
- The promoted GPU-driven path should use a canonical instance-slot space shared by renderable records, transform records, bounds/culling records, material references, picking IDs, and draw buckets.
- Heavy CPU scene data lives in the owning subsystem or runtime extraction pools; canonical ECS components keep source data/IDs, not graphics backend resources.

## Related references

- Frame graph details: [frame-graph.md](frame-graph.md).
- Historical migration docs: `legacy-rendering-architecture-migration.md`, `gpu-driven-modular-rendering-pipeline-plan.md`.

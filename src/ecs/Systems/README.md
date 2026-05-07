# ECS/Systems

This directory contains the `Systems` module/files.

## Contents

- `CMakeLists.txt`
- `ECS.System.RenderSync.cpp`
- `ECS.System.RenderSync.cppm`
- `ECS.System.TransformHierarchy.cpp`
- `ECS.System.TransformHierarchy.cppm`

## Render sync boundary

Per
[`GRAPHICS-028`](../../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md),
GPU-handle-touching render residency does not belong in ECS systems.
`ECS.System.RenderSync` may remain a CPU-only aggregation or forwarding seam if
needed, but calls to `GpuWorld`, `GpuAssetCache`, RHI managers, or graphics-owned
`GpuSceneSlot` storage are runtime responsibilities owned by
`Runtime.RenderExtraction` or a runtime residency sibling.

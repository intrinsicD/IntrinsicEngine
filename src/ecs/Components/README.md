# ECS/Components

This directory contains the `Components` module/files.

## Contents

- `CMakeLists.txt`
- `ECS.Component.AssetInstance.cppm`
- `ECS.Component.Collider.cppm`
- `ECS.Component.Culling.Local.cppm`
- `ECS.Component.Culling.Proxy.cppm`
- `ECS.Component.Culling.World.cppm`
- `ECS.Component.DirtyTags.cppm`
- `ECS.Component.GeometrySources.cppm`
- `ECS.Component.Hierarchy.cppm`
- `ECS.Component.Light.cppm`
- `ECS.Component.MetaData.cppm`
- `ECS.Component.Selection.cppm`
- `ECS.Component.ShadowCaster.cppm`
- `ECS.Component.Transform.Local.cppm`
- `ECS.Component.Transform.World.cppm`

## Render residency boundary

Per
[`GRAPHICS-028`](../../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md),
render-facing ECS components remain CPU-only. `AssetInstance::Source`,
`GeometrySources::*`, hierarchy/transform data, and `DirtyTags::*` may identify
what runtime extraction should consider, but canonical ECS components must not
store `GpuInstanceHandle`, `GpuGeometryHandle`, `RHI::BufferHandle`, bindless
indices, `GpuSceneSlot`, or renderer buffer names. Runtime owns any
entity-keyed graphics residency sidecar/cache and translates CPU dirty semantics
into graphics uploads.

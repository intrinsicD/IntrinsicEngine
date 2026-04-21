# ECS

`src_new/ECS` contains the entity-component infrastructure used by every
`src_new` subsystem that needs to query or mutate scene state. Components are
plain data; systems are stateless functions that operate on components.

## Public module surface

### Scene

- `Extrinsic.ECS.Scene.Handle`
- `Extrinsic.ECS.Scene.Registry`

### Components

- `Extrinsic.ECS.Component.Transform`
- `Extrinsic.ECS.Component.Hierarchy`
- `Extrinsic.ECS.Component.MetaData`
- `Extrinsic.ECS.Component.CpuGeometry`
- `Extrinsic.ECS.Component.RenderGeometry`

### Systems

- `Extrinsic.ECS.System.TransformHierarchy`
- `Extrinsic.ECS.System.RenderSync`

## Directory layout

```text
ECS.Scene.Handle.cppm
ECS.Scene.Registry.cppm
Components/
  ECS.Component.Transform.cppm
  ECS.Component.Hierarchy.cppm
  ECS.Component.MetaData.cppm
  ECS.Component.CpuGeometry.cppm
  ECS.Component.RenderGeometry.cppm
Systems/
  ECS.System.TransformHierarchy.{cppm,cpp}
  ECS.System.RenderSync.{cppm,cpp}
```

## Dependency note

`ECS` depends on `Core`. It does **not** depend on `Graphics`, `Platform`, or
`Runtime`. Render-side synchronization lives in `Systems/ECS.System.RenderSync`
and communicates with `Graphics` through data contracts carried on components
(`RenderGeometry`), not through direct imports of graphics internals.

## Asset references on components

Geometry-bearing components (`Mesh`, `Graph`, `PointCloud`, material/texture
references) store an `AssetId` — **never** a `BufferView`, bindless index, or
other GPU handle. `AssetId` is stable across hot-reload, device-lost, and
swapchain recreate; a GPU handle is not. Graphics resolves `AssetId →
BufferView` per frame via its `GpuAssetCache::TryGet`. See `CLAUDE.md` →
"Assets ↔ Graphics boundary".

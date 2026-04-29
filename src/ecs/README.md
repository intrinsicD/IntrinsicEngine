# ECS

`src/ecs` contains the entity-component infrastructure used by every
engine subsystem that needs to query or mutate scene state. Components are
plain data; systems are stateless functions that operate on components.

## Public module surface

### Scene

- `Extrinsic.ECS.Scene.Handle`
- `Extrinsic.ECS.Scene.Registry`

### Components

- `Extrinsic.ECS.Component.Transform`
- `Extrinsic.ECS.Component.Transform.WorldMatrix`
- `Extrinsic.ECS.Component.Hierarchy`
- `Extrinsic.ECS.Component.MetaData`
- `Extrinsic.ECS.Components.GeometrySources`
- `Extrinsic.ECS.Component.Culling.Local`
- `Extrinsic.ECS.Component.Culling.World`
- `Extrinsic.ECS.Component.Culling.Proxy`
- `Extrinsic.ECS.Component.AssetInstance`
- `Extrinsic.ECS.Component.Collider`
- `Extrinsic.ECS.Component.Light`
- `Extrinsic.ECS.Component.Selection`
- `Extrinsic.ECS.Component.ShadowCaster`
- `Extrinsic.ECS.Component.DirtyTags`

### Systems

- `Extrinsic.ECS.System.TransformHierarchy`
- `Extrinsic.ECS.System.RenderSync`

## Directory layout

```text
ECS.Scene.Handle.cppm
ECS.Scene.Registry.cppm
Components/
  ECS.Component.Transform.Local.cppm
  ECS.Component.Transform.World.cppm
  ECS.Component.Hierarchy.cppm
  ECS.Component.MetaData.cppm
  ECS.Component.GeometrySources.cppm
  ECS.Component.Culling.Local.cppm
  ECS.Component.Culling.World.cppm
  ECS.Component.Culling.Proxy.cppm
  ECS.Component.AssetInstance.cppm
  ECS.Component.Collider.cppm
  ECS.Component.Light.cppm
  ECS.Component.Selection.cppm
  ECS.Component.ShadowCaster.cppm
  ECS.Component.DirtyTags.cppm
Systems/
  ECS.System.TransformHierarchy.{cppm,cpp}
  ECS.System.RenderSync.{cppm,cpp}
```

## Dependency note

`ECS` depends on `Core`. It does **not** depend on `Graphics`, `Platform`, or
`Runtime`. Render-side synchronization lives in `Systems/ECS.System.RenderSync`
and communicates with `Graphics` through data contracts carried on components
(`GeometrySources` + culling/light tags), not through direct imports of graphics internals.

## Asset references on components

Geometry-bearing components (`Mesh`, `Graph`, `PointCloud`, material/texture
references) store an `AssetId` — **never** a `BufferView`, bindless index, or
other GPU handle. `AssetId` is stable across hot-reload, device-lost, and
swapchain recreate; a GPU handle is not. Graphics resolves `AssetId →
BufferView` per frame via its `GpuAssetCache::TryGet`. See `CLAUDE.md` →
"Assets ↔ Graphics boundary".

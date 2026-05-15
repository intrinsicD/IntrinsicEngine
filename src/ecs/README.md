# ECS

`src/ecs` contains the entity-component infrastructure used by every
engine subsystem that needs to query or mutate scene state. Components are
plain data; systems are stateless functions that operate on components.

## Public module surface

### Scene

- `Extrinsic.ECS.Scene.Handle`
- `Extrinsic.ECS.Scene.Registry`
- `Extrinsic.ECS.Scene.Bootstrap`

### Hierarchy

- `Extrinsic.ECS.Hierarchy.Structure` — pure linked-list primitives, descendant
  walks, and invariant checks. No transform dependency.
- `Extrinsic.ECS.Hierarchy.Mutation` — public `Attach` / `Detach` API. Composes
  structural mutation with world-position preservation across reparenting.

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
- `Extrinsic.ECS.Component.ProceduralGeometryRef`
- `Extrinsic.ECS.Component.Selection`
- `Extrinsic.ECS.Component.ShadowCaster`
- `Extrinsic.ECS.Component.DirtyTags`

### Systems

- `Extrinsic.ECS.System.TransformHierarchy`
- `Extrinsic.ECS.System.BoundsPropagation`
- `Extrinsic.ECS.System.RenderSync`

### Events

- `Extrinsic.ECS.Events` — CPU-only payload types for promoted scene
  mutations (`SelectionChanged`, `HoverChanged`, `EntitySpawned`,
  `GeometryModified`). Dispatch/queueing/subscription belong to
  `runtime`/`editor`; see `Events/README.md` for the `HARDEN-063`
  ownership decision and the events that are deliberately
  runtime/graphics-owned (`GpuPickCompleted`, `GeometryUploadFailed`).

## Directory layout

```text
ECS.Scene.Handle.cppm
ECS.Scene.Registry.cppm
ECS.Scene.Bootstrap.{cppm,cpp}
Hierarchy/
  ECS.Hierarchy.Structure.{cppm,cpp}
  ECS.Hierarchy.Mutation.{cppm,cpp}
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
  ECS.Component.ProceduralGeometryRef.cppm
  ECS.Component.Selection.cppm
  ECS.Component.ShadowCaster.cppm
  ECS.Component.DirtyTags.cppm
Events/
  ECS.Events.cppm
Systems/
  ECS.System.TransformHierarchy.{cppm,cpp}
  ECS.System.BoundsPropagation.{cppm,cpp}
  ECS.System.RenderSync.{cppm,cpp}
```

## Dependency note

The promoted ECS layer follows the contract from
[`/AGENTS.md §2`](../../AGENTS.md): `ecs -> {core, geometry}`. ECS modules
must not import `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`,
`Extrinsic.Runtime.*`, `Extrinsic.Platform.*`, `Extrinsic.App.*`, or any
live `Extrinsic.Asset.*` modules. Render-side synchronization lives in
`Systems/ECS.System.RenderSync`, a CPU-only tag-forwarding pass that
translates `Transform::WorldUpdatedTag` into `DirtyTags::DirtyTransform`
for the runtime render-extraction lane to drain (see
`Systems/README.md` and the `HARDEN-066` policy decision). All
communication with `Graphics` flows through data contracts carried on
components (`GeometrySources` + culling/light tags), not through direct
imports of graphics internals.

The contract test
[`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`](../../tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp)
enforces the prohibited-import set in addition to the structural
`tools/repo/check_layering.py` sweep, so CPU-only test runs catch upward
imports before they reach the layering allowlist.

### Linked dependencies

- `src/ecs/CMakeLists.txt` links `EnTT::EnTT` (header-only) for the
  `entt::registry` storage used by the typed `Registry` wrapper.
- `src/ecs/Components/CMakeLists.txt` links `IntrinsicGeometry` and
  `glm::glm` only — components import `Geometry.*` types (Sphere, AABB,
  OBB, Octree, ConvexHull, Properties) and use `glm` for transform math.
  ECS components do **not** link `ExtrinsicAssets` and **not**
  `ExtrinsicCore`; ExtrinsicCore enters `ExtrinsicECS` transitively via
  `Systems/CMakeLists.txt` for the `Extrinsic.Core.FrameGraph` import in
  `ECS.System.TransformHierarchy`.
- `src/ecs/Systems/CMakeLists.txt` links `ExtrinsicCore` for the FrameGraph
  type used by `RegisterSystem`.

## Asset references on components

Geometry-bearing components (`Mesh`, `Graph`, `PointCloud`, material/texture
references) store an `AssetId` — **never** a `BufferView`, bindless index, or
other GPU handle, and **never** a live `AssetService` pointer. `AssetId` is
stable across hot-reload, device-lost, and swapchain recreate; a GPU handle is
not. Graphics resolves `AssetId → BufferView` per frame via its
`GpuAssetCache::TryGet`. See `CLAUDE.md` → "Assets ↔ Graphics boundary".

`ECS.Component.AssetInstance::Source::AssetId` is currently a raw
`std::uint32_t` engine-wide stable ID rather than the typed
`Extrinsic::Assets::AssetId` (`Core::StrongHandle<AssetTag>`) used inside
the graphics layer. Promoting that field to the typed handle would create
an `ecs -> assets` dependency edge that the current contract does not
permit (see `tools/repo/check_layering.py::ALLOWED_DEPS`). Adopting the
typed handle on ECS is a follow-up architectural decision rather than a
mechanical change; until that decision is recorded, ECS keeps the raw
stable ID and the runtime assembles the typed handle at the
`ecs -> runtime -> graphics` seam.

## Component boundary contract

Canonical ECS components store CPU-only descriptors and stable IDs. They
must not embed:

- `PhysicsBodyHandle`, `RigidBodyHandle`, broadphase proxy handles,
  contact caches, island IDs, solver indices, or any other physics-world
  / solver-owned state.
- `RhiTextureHandle`, `RhiBufferHandle`, bindless indices, or other RHI /
  GPU handles.
- Live `AssetService` pointers or per-frame runtime sync sidecars.

Graphics-side mirrors of ECS data (e.g. `Graphics.Component.GpuSceneSlot`)
live in `src/graphics/renderer/Components/` and are owned by the graphics
layer; they are not canonical ECS components.

### Geometry types ECS components may store directly

ECS components import the following CPU-only `Geometry.*` types because they
describe scene-side spatial state that has no graphics or runtime
counterpart:

- `Geometry::Sphere`, `Geometry::AABB`, `Geometry::OBB` — bounding volumes
  on `Culling.Local` / `Culling.World` and `Light::AmbientLight`.
- `Geometry::Octree`, `Geometry::ConvexHull` — spatial proxies on
  `Culling.Proxy`.
- `Geometry::PropertySet` (via `ObserverPtr`) — non-owning property set
  views on `GeometrySources::{Vertices,Edges,Faces,Halfedges,Tetrahedra}`.

Adding new `Geometry.*` imports to ECS requires the new dependency to be
data-only (CPU descriptor or non-owning view) and to be motivated by the
scene-side use case, not by graphics or solver consumers.

## Collider vs rigid-body authoring

`ECS.Component.Collider` stores **CPU geometric descriptors only** — currently
a vector of `Geometry::Sphere`. The promoted ECS layer does not own the
authoring contract for rigid-body solver state: that is governed by
[`HARDEN-064`](../../tasks/backlog/ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md),
which is itself gated on the physics layer ownership decision tracked by
[`ARCH-001`](../../tasks/backlog/physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md).

Until the physics ownership decision lands, ECS is forbidden from storing
rigid-body or solver-owned state on collider components; the contract
test enforces this by rejecting `RigidBody*`, `PhysicsBody*`, `Broadphase*`,
`ContactCache*`, `IslandId`, and `SolverIndex` mentions inside `src/ecs`.

## Scene hierarchy vs collider hierarchy

`ECS.Component.Hierarchy` describes the **scene-graph** parent/child
relationship used by the promoted `TransformHierarchy` traversal: parent,
first-child, sibling links, child count, and a stable `EntityHandle`. It
does **not** implicitly define a compound-collider topology. A future
physics layer that introduces compound colliders must declare its own
parent/child relationship — likely as a separate component owned by the
physics or runtime layer — rather than re-using the scene hierarchy as
the collider tree. The contract test rejects `Collider`, `Compound`, and
`RigidBody`/`PhysicsBody` mentions inside the hierarchy component to
preserve this separation.

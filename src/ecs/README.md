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
- `Extrinsic.ECS.Components.GeometrySources` — per-domain owned
  `PropertySet` components (`Vertices`, `Edges`, `Halfedges`, `Faces`,
  `Nodes`) plus canonical `PropertyNames` keys, domain markers
  (`HasMeshTopology`, `HasGraphTopology`), and `BuildConstView`/
  `BuildMutableView` domain detection.
- `Extrinsic.ECS.Components.GeometrySourcesPopulate` — promoted
  `PopulateFromMesh` / `PopulateFromGraph` / `PopulateFromCloud` helpers
  that copy a `Geometry::HalfedgeMesh::Mesh`, `Geometry::Graph::Graph`,
  or `Geometry::PointCloud::Cloud` into the ECS-owned PropertySets
  (HARDEN-065 slice 2).
- `Extrinsic.ECS.Component.Culling.Local`
- `Extrinsic.ECS.Component.Culling.World`
- `Extrinsic.ECS.Component.Culling.Proxy`
- `Extrinsic.ECS.Component.AssetInstance`
- `Extrinsic.ECS.Component.Collider`
- `Extrinsic.ECS.Component.Light`
- `Extrinsic.ECS.Component.ProceduralGeometryRef`
- `Extrinsic.ECS.Component.Selection`
- `Extrinsic.ECS.Component.ShadowCaster`
- `Extrinsic.ECS.Component.StableId` — 128-bit UUID-shaped durable identity
  value type (`HARDEN-068-Impl-A`). Optional sparse component; not assigned
  by default bootstrap. See `Components/README.md` and the "Stable identity
  and scene metadata" section below.
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
  ECS.Component.GeometrySourcesPopulate.{cppm,cpp}
  ECS.Component.Culling.Local.cppm
  ECS.Component.Culling.World.cppm
  ECS.Component.Culling.Proxy.cppm
  ECS.Component.AssetInstance.cppm
  ECS.Component.Collider.cppm
  ECS.Component.Light.cppm
  ECS.Component.ProceduralGeometryRef.cppm
  ECS.Component.Selection.cppm
  ECS.Component.ShadowCaster.cppm
  ECS.Component.StableId.cppm
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

`HARDEN-068` slice 1 (Decision 5, see
[`tasks/active/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`](../../tasks/active/HARDEN-068-ecs-stable-identity-and-scene-metadata.md))
explicitly does **not** widen this contract under HARDEN-068. If a
future consumer wants the typed handle, that becomes a separate
`ARCH-*` task that owns the layering allowlist change and the
contract-test update; HARDEN-068 and its implementation children stay
inside `ecs -> {core, geometry}`.

## Stable identity and scene metadata

Entity-stable identity used for scene save/load, undo/redo, prefab
references, hot reload, and external references is **separate** from
the volatile `entt::entity` value. `HARDEN-068` slice 1 (see
[`tasks/active/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`](../../tasks/active/HARDEN-068-ecs-stable-identity-and-scene-metadata.md))
records the five contract decisions:

1. **Shape (Decision 1).** `StableId` is a 128-bit UUID-shaped
   value type — a `struct StableId { std::uint64_t High; std::uint64_t Low; }`
   with `kInvalidStableId == StableId{0u, 0u}`, `IsValid`,
   `operator<=>`, and an explicit `StableIdHash` mirroring the
   `Extrinsic.Core.StrongHandle` exported-hasher pattern at
   `src/core/Core.StrongHandle.cppm`.
2. **Optionality (Decision 2).** `StableId` is an **optional sparse
   `entt`-component**, not a mandatory registry-wide field. The
   default scene bootstrap (`Extrinsic.ECS.Scene.Bootstrap::CreateDefault`)
   does not assign one; authoring/runtime code that needs durable
   references opts in.
3. **Reference semantics (Decision 3).** ECS owns only the value
   type plus equality/hash. Any `StableId → entt::entity` lookup
   sidecar (scene-local map, prefab-aware resolver, etc.) lives in
   `src/runtime/`, never in `src/ecs/`. Persisted-to-disk or
   crossing-hot-reload references store `StableId`; in-process
   references may still store `entt::entity`.
4. **Component placement (Decision 4).** `StableId` lives in its
   own `Extrinsic.ECS.Component.StableId` module. `MetaData` stays
   the bootstrap naming contract (cheap, common, present on every
   default entity); future authoring metadata (`SerializationHints`,
   `SceneSource`, prefab provenance) lands as **separate focused
   components**, not extensions to `MetaData`.
5. **Asset typing (Decision 5).** `AssetInstance::Source::AssetId`
   remains a raw `std::uint32_t`; see the "Asset references on
   components" section above for the cross-link.

**Status.** Slice 1 recorded the decisions. Slice 2
(`HARDEN-068-Impl-A`) added the `Extrinsic.ECS.Component.StableId`
payload module (`src/ecs/Components/ECS.Component.StableId.cppm`)
exporting `Extrinsic::ECS::Components::StableId`, `kInvalidStableId`,
`IsValid`, defaulted `operator<=>`/equality, and the
`StableIdHash` exported hasher (mirrors the `Extrinsic.Core.StrongHandle`
pattern). The payload is a pure CPU value type — no `entt`, `geometry`,
`assets`, `runtime`, `graphics`, or `platform` imports — so serializers,
editors, and runtime helpers can consume it without taking on those
dependencies. The contract test
`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`
(`StableIdPayloadStaysCpuOnly`) enforces the no-`entt` rule on this one
module specifically; the existing directory-recursive sweep enforces the
shared `ecs -> {core, geometry}` import constraint. CPU-only `unit;ecs`
payload tests live in `tests/unit/ecs/Test.ECS.StableIdentity.cpp`.
Generator helpers and the optional `Runtime::StableIdRegistry` sidecar
ship in **HARDEN-068-Impl-B** (slice 3) only when a concrete
serializer/selection consumer demands them.

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

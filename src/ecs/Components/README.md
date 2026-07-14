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
- `ECS.Component.GeometrySourcesPopulate.cppm` + `.cpp`
- `ECS.Component.Hierarchy.cppm`
- `ECS.Component.Light.cppm`
- `ECS.Component.MetaData.cppm`
- `ECS.Component.ProceduralGeometryRef.cppm`
- `ECS.Component.RigidBody.cppm`
- `ECS.Component.Selection.cppm`
- `ECS.Component.ShadowCaster.cppm`
- `ECS.Component.SpatialDebugBinding.cppm`
- `ECS.Component.StableId.cppm`
- `ECS.Component.Transform.Local.cppm`
- `ECS.Component.Transform.World.cppm`

## Stable identity vs `MetaData`

`HARDEN-068` (see
[`tasks/archive/HARDEN-068-ecs-stable-identity-and-scene-metadata.md`](../../../tasks/archive/HARDEN-068-ecs-stable-identity-and-scene-metadata.md))
records the contract for entity-stable identity:

- `Extrinsic.ECS.Component.MetaData` (`ECS.Component.MetaData.cppm`)
  stays the **bootstrap naming contract** — every default-bootstrapped
  entity carries `MetaData::EntityName`. `MetaData` is not extended
  with stable-identity, prefab, or serialization fields under
  HARDEN-068 (Decision 4).
- `Extrinsic.ECS.Component.StableId`
  (`ECS.Component.StableId.cppm`, landed by HARDEN-068-Impl-A) carries a
  128-bit UUID-shaped `StableId` value type. It is an **optional sparse
  component** (Decision 2) — authoring/runtime code opts in only when a
  serializer / undo / prefab / external-reference consumer needs
  durability across `entt::entity` recycling, save/load, or hot
  reload.
- ECS owns only the value type + `kInvalidStableId` + `IsValid` +
  defaulted equality / `operator<=>` + the exported `StableIdHash`
  hasher (Decision 3). The payload header imports neither `entt` nor
  any higher-layer module so it remains usable from serializer / editor /
  runtime helpers; the contract test
  [`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`](../../../tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp)
  (`StableIdPayloadStaysCpuOnly`) enforces that rule. Any
  `StableId → entt::entity` lookup sidecar lives in `src/runtime/`,
  not in `src/ecs/`.
- Future authoring metadata (`SerializationHints`, `SceneSource`,
  prefab provenance) likewise lands as **separate focused
  components** opened by their own slices when a concrete consumer
  exists; do not bundle them onto `MetaData` or a single
  `EntityIdentity` component.

`AssetInstance::Source::AssetId` stays a raw `std::uint32_t` per the
existing `HARDEN-062` decision (cross-linked from
[`../README.md`](../README.md) "Asset references on components"). The
`ecs -> assets` dependency contract is not widened under HARDEN-068
(Decision 5); widening would require a separate `ARCH-*` task that
owns the layering allowlist change.

## Collider and rigid-body authoring

`ECS.Component.Collider` and `ECS.Component.RigidBody` define the ECS-owned
physics authoring contract from
[`HARDEN-064`](../../../tasks/archive/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md).
They intentionally stop at CPU descriptors:

- `Collider::Component` is a list of `ShapeDescriptor` child shapes. The
  first-phase shape set is sphere, capsule, and box/OBB; each child carries its
  own local pose, material, collision filter, contact/rest offsets, trigger
  state, and enabled bit.
- `RigidBody::Component` carries motion intent (`Static`, `Kinematic`, or
  `Dynamic`), mass policy, velocities, damping, gravity scale, sleep flags,
  CCD intent, contact participation, and enabled state.
- Component combinations are classified in ECS only as authoring diagnostics:
  collider-only static/trigger authoring, explicit static, kinematic, dynamic,
  non-contacting body state, or missing-collider-for-contacting-body.

No physics-world handle, broadphase proxy, contact cache, island ID, solver
index, runtime sidecar, graphics handle, or RHI handle may be stored here.
`src/physics` owns world/solver state, and `src/runtime` owns the
ECS-to-physics synchronization bridge.

## Render residency boundary

Per
[`GRAPHICS-028`](../../../tasks/archive/GRAPHICS-028-ecs-renderable-residency-bridge.md),
render-facing ECS components remain CPU-only. `AssetInstance::Source`,
`GeometrySources::*`, hierarchy/transform data, and `DirtyTags::*` may identify
what runtime extraction should consider, but canonical ECS components must not
store `GpuInstanceHandle`, `GpuGeometryHandle`, `RHI::BufferHandle`, bindless
indices, `GpuSceneSlot`, or renderer buffer names. Runtime owns any
entity-keyed graphics residency sidecar/cache and translates CPU dirty semantics
into graphics uploads.

`ECS.Component.ProceduralGeometryRef` carries the CPU-only authoring data for
runtime-owned procedural geometry sources (kind + POD parameters) per the
GRAPHICS-030 planning contract. It imports `Extrinsic.Core.*` only; the runtime
descriptor/cache/packer that consumes it lives in `src/runtime/` and is the only
writer of the GPU-side residency handles.

`ECS.Component.SpatialDebugBinding` (`RUNTIME-082` Slice D) carries the
runtime spatial-debug adapter pump's renderable↔geometry-tree binding:
`Kind` (BVH / KDTree / Octree / ConvexHull) plus a `RegistryKey`
(`std::uint64_t`) resolved per frame through the
`SpatialDebugAdapterRegistry` owned by `RenderExtractionCache`, plus the
per-binding adapter options (`LeafOnly` / `OccupancyOnly` / `MaxDepth`).
The component is plain-old-data and imports nothing beyond `<cstdint>`; the
adapter instances themselves live in runtime under
`Extrinsic.Runtime.SpatialDebugAdapters` per the layering contract.

## Geometry dirty-domain stamping

`Extrinsic.ECS.Component.DirtyTags` exposes five idempotent producer-side
helpers that geometry mutation sites call to stamp the per-domain re-upload
markers without depending on runtime or graphics state:

- `MarkVertexPositionsDirty(registry, entity)` — `DirtyVertexPositions`.
- `MarkVertexAttributesDirty(registry, entity)` — `DirtyVertexAttributes`.
- `MarkEdgeTopologyDirty(registry, entity)` — `DirtyEdgeTopology`.
- `MarkFaceTopologyDirty(registry, entity)` — `DirtyFaceTopology`.
- `MarkGpuDirty(registry, entity)` — `GpuDirty` (full geometry re-upload).

Each helper `emplace_or_replace`s the corresponding tag so repeated calls are
safe. The fine-grained domain tags are independent partial-upload markers and
do not implicitly stamp `GpuDirty`; callers that need a full re-upload signal
call `MarkGpuDirty` explicitly.

Clearing-side ownership stays outside ECS: downstream consumers — runtime
render extraction today (see
[`Runtime.RenderExtraction::ExtractAndSubmit`](../../runtime/Runtime.RenderExtraction.cpp))
and any future GPU residency drain — remove these tags after the
corresponding upload, mirroring the existing `DirtyTags::DirtyTransform`
drain that `RenderSync::OnUpdate` produces and `RenderExtraction` consumes
(`HARDEN-066`).

## Geometry source ownership

`HARDEN-065` slice 2 closed the borrowed-vs-owned decision for promoted
`GeometrySources`: the per-domain components (`Vertices`, `Edges`,
`Halfedges`, `Faces`, `Nodes`) now own a `Geometry::PropertySet` directly,
matching the legacy `ECS::Components::GeometrySources` shape. The entity is
the authoritative CPU geometry source after a `PopulateFrom*` call, so the
originating mesh/graph/cloud object can be discarded without invalidating
the ECS view.

`Extrinsic.ECS.Components.GeometrySourcesPopulate` provides the promoted
population helpers (declared in `ECS.Component.GeometrySourcesPopulate.cppm`,
implemented in the matching `.cpp`):

- `PopulateFromMesh(registry, entity, mesh)` — emplaces `Vertices`,
  `Edges`, `Halfedges`, and `Faces`. Each per-domain `PropertySet` is
  copied from the source `Geometry::HalfedgeMesh::Mesh` so user-defined
  properties (colors, labels, vector fields, …) survive the promotion.
  Canonical keys: `v:position`, `e:v0`/`e:v1`, `h:to_vertex`/`h:next`/
  `h:face`, `f:halfedge`.
- `PopulateFromGraph(registry, entity, graph)` — emplaces `Nodes` and
  `Edges` plus the `HasGraphTopology` marker (graph halfedges remain
  internal to `Geometry::Graph` and are not promoted to GeometrySources;
  the marker lets `BuildConstView`/`BuildMutableView` resolve
  `Domain::Graph` without a `Halfedges` PropertySet). Calls
  `graph.GarbageCollection()` if `HasGarbage()` so the resulting
  PropertySets are contiguous.
- `PopulateFromCloud(registry, entity, cloud)` — emplaces `Vertices` with
  canonical `v:position` (and `v:normal` when `HasNormals()`), preserving
  the full `PointProperties()` PropertySet.

Canonical key constants live in
`Extrinsic.ECS.Components.GeometrySources::PropertyNames`
(`kPosition`, `kNormal`, `kMeanCurvature`, `kGaussianCurvature`,
`kPrincipalDir1`, `kPrincipalDir2`, `kEdgeV0`, `kEdgeV1`,
`kHalfedgeToVertex`, `kHalfedgeNext`, `kHalfedgeFace`, `kFaceHalfedge`);
read sites must prefer these constants over inline string literals.
The curvature keys are mesh-vertex properties published by the runtime/editor
curvature command: `v:mean_curvature` and `v:gaussian_curvature` store
count-matched `double` scalars, while `v:principal_dir1` and
`v:principal_dir2` store count-matched `glm::vec3` tangent directions when the
geometry curvature backend publishes directions.

`BuildConstView` / `BuildMutableView` keep the exact-domain classifier in
`ActiveDomain`: a full mesh, graph, or point cloud resolves to one mutually
exclusive domain, while partial or mixed source sets resolve to `Unknown`.
Use `BuildSourceAvailability` when a consumer needs to know which CPU sources
are actually present. The availability contract reports provenance (`Mesh`,
`Graph`, `PointCloud`, `Unknown`) separately from capabilities
(`VertexPoints`, `NodePoints`, `Edges`, `Halfedges`, `Faces`), so a mesh can
satisfy point/edge consumers without pretending to be a point cloud or graph,
and graph entities populated without a `Halfedges` property set do not
advertise halfedge-source availability.

Each populate helper drops the entity's prior `GeometrySources`
components (`Vertices`/`Edges`/`Halfedges`/`Faces`/`Nodes`) and
topology markers (`HasMeshTopology`/`HasGraphTopology`) before
emplacing the new domain, so a re-population from a different
domain (mesh→cloud, graph→cloud, mesh→graph, etc.) cannot leak stale
topology into `BuildConstView`/`BuildMutableView`. The reset is a
silent no-op on first-population entities.

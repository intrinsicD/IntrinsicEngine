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

`ECS.Component.ProceduralGeometryRef` carries the CPU-only authoring data for
runtime-owned procedural geometry sources (kind + POD parameters) per the
GRAPHICS-030 planning contract. It imports `Extrinsic.Core.*` only; the runtime
descriptor/cache/packer that consumes it lives in `src/runtime/` and is the only
writer of the GPU-side residency handles.

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
(`kPosition`, `kNormal`, `kEdgeV0`, `kEdgeV1`, `kHalfedgeToVertex`,
`kHalfedgeNext`, `kHalfedgeFace`, `kFaceHalfedge`); read sites must
prefer these constants over inline string literals.

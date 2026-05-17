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

The promoted ownership decision for `GeometrySources` (borrowed views,
owned copies, or a split) and the population helpers (`PopulateFromMesh`,
`PopulateFromGraph`, `PopulateFromCloud`) remain deferred to
[`HARDEN-065`](../../../tasks/active/HARDEN-065-ecs-geometry-source-population-and-dirty-domains.md)
slice 2; the current promoted `GeometrySources` retains the non-owning
`ObserverPtr<Geometry::PropertySet>` shape.

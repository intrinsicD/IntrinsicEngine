# Retained Geometry Lifecycle Consolidation Audit

This document records the current audit of duplicate retained-geometry upload and lifecycle code paths across:

- `GraphLifecycle`
- `PointCloudLifecycle`
- `MeshViewLifecycle`
- `PropertySetDirtySync`

It exists to close the near-term TODO item:

- "Audit duplicate upload/lifecycle code paths between retained geometry systems and document consolidation plan."

## Scope

The audit focuses on CPU-side retained geometry preparation and ECS lifecycle wiring, not on pass shaders or render-graph execution order.

The concrete systems reviewed were:

- `src/Runtime/Graphics/Graphics.Systems.GraphLifecycle.cpp`
- `src/Runtime/Graphics/Graphics.Systems.PointCloudLifecycle.cpp`
- `src/Runtime/Graphics/Graphics.Systems.MeshViewLifecycle.cpp`
- `src/Runtime/Graphics/Graphics.Systems.PropertySetDirtySync.cpp`
- `src/Runtime/Graphics/Graphics.LifecycleUtils.hpp`
- `src/Runtime/Graphics/Graphics.GraphPropertyHelpers.hpp`

## Summary

The retained-geometry systems already share a useful common substrate:

- GPUScene slot allocation/release lives in `Graphics.LifecycleUtils.hpp`.
- Visibility-aware pass component population also lives in `Graphics.LifecycleUtils.hpp`.
- Graph property extraction is already centralized in `Graphics.GraphPropertyHelpers.hpp`.

However, the audit found three remaining duplication clusters:

1. **Attribute extraction duplication**
   - Point-cloud per-point color/radius extraction existed in both `PointCloudLifecycle` and `PropertySetDirtySync`.
   - Graph extraction is already centralized, so point clouds were the obvious missing peer.

2. **Geometry rewrite / teardown duplication**
   - Graph and point-cloud systems both manually clear cached attribute vectors, reset counts, and release `GeometryHandle`s when the authoritative source becomes empty or invalid.
   - The exact fields differ, but the state-machine shape is the same: release old GPU state -> clear cached derived data -> reset counts/flags -> skip pass population.

3. **Full-upload orchestration duplication**
   - All three systems follow the same high-level lifecycle:
     1. detect dirty authoritative data
     2. validate / compact / derive upload payloads
     3. release old GPU geometry
     4. call `GeometryGpuData::CreateAsync(...)`
     5. store handles + cached CPU-side derived data
     6. allocate GPUScene slot if needed
     7. populate typed per-pass ECS components
   - The per-domain payload construction differs, but the orchestration skeleton is structurally the same.

## Current Duplicate Hotspots

## A. Point-cloud attribute extraction

Before this audit cleanup, these two blocks were effectively duplicated:

- `PointCloudLifecycle` phase-1 extraction
- `PropertySetDirtySync::SyncPointCloudAttributes`

Shared behavior:

- default `VertexColors.PropertyName` to `p:color` when the cloud exposes colors
- map colors through `Graphics::ColorMapper`
- copy radii from the authoritative cloud when available

This was the lowest-risk consolidation target because it has:

- identical source data (`PointCloud::Cloud`)
- identical output data (`CachedColors`, `CachedRadii`)
- no GPU/RHI coupling
- no ECS mutation beyond writing the cached vectors

## B. Release-and-reset branches

### Graph

`GraphLifecycle` has a local `releaseGraphGpu(...)` lambda that:

- removes vertex geometry
- removes edge geometry
- clears cached edge/node attributes
- resets counts
- clears `GpuDirty`

### Point cloud

`PointCloudLifecycle` has an inline empty/null-cloud branch that:

- removes point geometry
- clears cached point attributes
- resets counts / normal flags
- clears `GpuDirty`

These are conceptually parallel, but not yet factored. They are good future consolidation candidates once we settle a reusable "domain reset policy" shape that does not over-generalize across distinct component layouts.

## C. Upload orchestration skeleton

`GraphLifecycle`, `PointCloudLifecycle`, and `MeshViewLifecycle` all implement a near-identical control flow:

$$
\text{Dirty} \rightarrow \text{Validate/Extract} \rightarrow \text{Upload} \rightarrow
\text{AllocateSlot} \rightarrow \text{PopulatePassComponents}
$$

The complexity is linear in the authoritative element count:

- graph upload prep: $O(|V| + |E|)$ time, $O(|V| + |E|)$ temporary storage
- point-cloud upload prep: $O(|P|)$ time, $O(|P|)$ temporary storage
- mesh edge-view derivation from indexed triangles: $O(|T|)$ average-time extraction with hash-backed deduplication, $O(|E|)$ temporary storage

We should not collapse these systems into one polymorphic mega-helper. The data products differ too much:

- graphs build both point and line views from graph topology
- point clouds build only point views and may operate on span-backed authoritative storage
- mesh view lifecycle builds derived topology views from existing surface GPU geometry plus collision indices

The right target is **shared phase helpers**, not a unified generic lifecycle class.

## Recommended Consolidation Plan

The recommended path is an **incremental O2-style medium refactor**, not a full rewrite.

### Phase 1 — Complete now

- Centralize point-cloud attribute extraction in a dedicated helper header.
- Keep graph extraction in `Graphics.GraphPropertyHelpers.hpp`.
- Keep existing lifecycle helpers in `Graphics.LifecycleUtils.hpp`.

This change is implemented together with this audit.

### Phase 2 — Introduce authoritative-data extraction helpers

Add narrowly-scoped, non-virtual helpers for each authoritative domain:

- `BuildGraphUploadPayload(...)`
- `BuildPointCloudUploadPayload(...)`
- `BuildMeshEdgeViewUploadPayload(...)`

Each helper should return a domain-specific payload/result struct, for example:

```cpp
struct GraphUploadPayload
{
    GeometryUploadRequest VertexUpload;
    GeometryUploadRequest EdgeUpload;
    std::vector<uint32_t> CachedNodeColors;
    std::vector<float> CachedNodeRadii;
    std::vector<uint32_t> CachedEdgeColors;
    uint32_t VertexCount = 0;
    uint32_t EdgeCount = 0;
};
```

Why this shape:

- preserves SoA-friendly temporary payloads
- keeps RHI upload requests explicit
- isolates geometry-domain extraction from ECS mutation
- makes narrow tests easy

### Phase 3 — Factor release/reset helpers by domain

Add one reset helper per retained-data component instead of a generic template:

- `ResetGraphGpuState(...)`
- `ResetPointCloudGpuState(...)`
- `ResetMeshViewState(...)`

These should:

- release owned `GeometryHandle`s
- zero authoritative cached counts
- clear cached visualization vectors
- normalize flags (`GpuDirty`, `HasGpuNormals`, etc.)

Do **not** try to erase type differences with traits or inheritance. The structs are small and explicit; clarity beats meta-programming here.

### Phase 4 — Extract upload commit helpers

Once payload builders exist, factor the common commit shape:

1. remove previous geometry if needed
2. issue `GeometryGpuData::CreateAsync(...)`
3. store returned handles
4. update derived counts and caches
5. run `TryAllocateGpuSlot(...)`
6. run `PopulateOrRemovePassComponent(...)`

Potential helper boundaries:

- `CommitPointUpload(...)`
- `CommitLineIndexUpload(...)`
- `CommitRetainedGeometryFailure(...)`

These helpers should stay small and concrete. They should not own control flow for all domains.

## Non-Goals

The audit recommends **against**:

- a single base class for lifecycle systems
- virtual dispatch in hot per-frame lifecycle code
- traits-heavy generic upload frameworks
- merging mesh/graph/point-cloud authoritative component types
- hiding `GeometryUploadRequest` behind opaque abstractions

Those options would reduce duplication on paper while increasing coupling, compile cost, and debugging friction.

## Decision

Adopt the following near-term policy:

1. centralize pure data extraction duplication first
2. keep domain-specific lifecycle state explicit
3. factor only shared orchestration seams that are already stable
4. prefer small helper headers/functions over framework-style abstraction

This keeps the architecture aligned with the engine’s data-oriented, explicit-subsystem style while still shrinking the most obvious duplicated retained-geometry code paths.

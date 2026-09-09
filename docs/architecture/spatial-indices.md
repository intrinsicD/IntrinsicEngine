# Spatial indices

See the [consumer inventory](spatial-index-consumers.md) for current and planned
uses, missing query capabilities, ownership decisions and linked open tasks.

The Sandbox composes `Runtime.SpatialIndexCache`, a concrete runtime module
that registers itself as a service. It owns lazy entity point indices.
Geometry components contain CPU geometry; GPU buffers and device lifetimes
belong to the runtime cache and graphics workspace, outside ECS components.

Consumers call `Acquire(world, entity, canonicalPositionProperty)`. A position
slot may bind any compatible float3 property on mesh vertices, edges,
halfedges or faces, graph nodes, edges or halfedges, or point-cloud points.
The shared canonical property resolver determines eligibility. Deleted slots
are omitted and results retain the original element indices.

The cache distinguishes world and full entity generation, domain, property
name, kind, cardinality, position revision, and deletion revision. Repeated
acquisitions reuse unchanged input. Attribute edits to other properties do
not rebuild the tree. Source mutation makes old handles unusable; acquiring
again builds a new snapshot. Maintenance prunes stale entries, including
destroyed worlds and entities. Shutdown withdraws the service and releases
its GPU resources before device teardown.

`Nearest`, `KNearest` and `Radius` return CPU results or `nullopt` for stale handles or
invalid queries. `RecordGpuBuild` lazily creates/uploads live positions and
source-slot mapping, then records the hierarchy. `RecordGpuQueries` also
records a batched query into caller-owned buffers. Both GPU operations return
false when unavailable; they never substitute a CPU result. `GpuView` lets a
method record a custom traversal using the existing hierarchy. Its borrowed
buffer address is valid only while the cache entry/device remains alive and
must not be kept across mutation, pruning, or shutdown.

GPU calls belong to the device's owning command stream. The caller must submit
recorded commands, keep input/output buffers alive until execution retires,
and wait for producer completion before readback. A recorded view is not a
CPU-observed completion token. These are reusable compute operations; callers
schedule them through the existing runtime GPU participant path when running
inside the frame loop. They do not alter the rendering frame recipe.

All distances are Euclidean in the bound property's coordinate space. Entity
transforms alone do not invalidate a local-space index. Transforming a query
into local space preserves world nearest ordering only for a rigid transform
or uniform scale; nonuniform scale requires an explicit distance metric or
separately indexed world-space positions.

## Method integration

| Surface | Current implementation |
| --- | --- |
| Least structured input | Float3 span or strided GPU view; no topology requirement |
| Compatible entity sources | All canonical point-valued element domains, through shared property resolution |
| Runtime owner | `SpatialIndexCache` service/module for stable geometry; `ClusteringGpuState` for moving centroids |
| Config/agent | Existing `sandbox.clustering` validated backend/parameter/property config selects CPU reference or Vulkan compute |
| UI | Existing K-Means method panels use that config and `ClusteringService`; Vulkan assignment uses LBVH |
| Publication | Cache queries return original source slots; k-means retains its existing atomic named-property publication and stale-source checks |
| End-to-end tests | `Test.SpatialIndexCache.cpp`, `Test.PointLBVHGpuSmoke.cpp`, `Test.ClusteringServiceGpuSmoke.cpp` |

K-means uses a private `Graphics::PointLbvhWorkspace`: its centroids move each
iteration, so it rebuilds the hierarchy before each assignment while reusing
allocations. This uses the same build and traversal shaders as entity queries.
Existing requested/actual/fallback reporting remains; successful Vulkan
completion diagnostics identify LBVH assignment. CPU fallback is explicit
when Vulkan is unavailable or its supported input bounds are exceeded.

## Construction and limits

The [method package](../../methods/geometry/point_lbvh/README.md) fixes the
Karras 2012 radix-tree formulation. The CPU oracle is an independent exhaustive
scan. The GPU builds bounds, Morton keys, sort order, topology, and node bounds
without CPU sorting or hierarchy construction. Duplicate Morton codes append
the original compact index, limiting binary radix depth to 62 and permitting
a 64-entry traversal stack.

The current GPU implementation uses a bitonic sorting network and independent
sorted-range bound unions. These prioritize deterministic, portable execution;
they are not an optimized radix-sort or bottom-up atomic bounds implementation.
No speed improvement is asserted. Bounds reduction starts with one workgroup;
large-input build performance is not yet characterized by the smoke workload.

Nearest ties choose the smallest source index. Radius queries are inclusive
and keep the lowest source indices in sorted order. Headers report total hits
even when capacity is zero or exceeded. GPU invalid-input headers have status
1; valid headers have status 0. Empty trees return no hits. Coordinates must
be finite and within +/-1e18; radii are finite, nonnegative and at most 1e18.
The GPU caps points/queries at 2^20 and radius capacity at 1024; the CPU tree
caps points at 2^24. Invalid CPU builds clear previous contents.

This point index does not replace the existing CPU median-split `Geometry.BVH`
used for face/edge bounds, or `Geometry.KDTree` consumers. It supplies nearest
k-nearest and radius queries. Triangle distance, ray traversal and renderer
scene acceleration require other primitives and traversal.

## Consumer leases and framed batches

ICP and point PCA use immutable `Snapshot` leases and framed GPU batches:
`QueueGpuNearest`, `QueueGpuKNearest` and `QueueGpuRadius`. A completed batch
can be reused at the same query count and capacity without reallocating buffers. The cache owns its JobService GPU
participant, producer/readback ordering and device shutdown. Batches retain
entries until completion, so pruning does not invalidate submitted resources.
Results retain original property row IDs; CPU snapshot indices are compact and
carry an explicit `Slots` mapping.

`Acquire(..., SpatialIndexSpace::EntityTransform)` indexes transformed positions
and includes the entity TRS matrix in cache freshness. This preserves the
registration Euclidean metric under nonuniform scale. The default `Property`
space remains independent of entity transforms. See [registration](registration.md).

## k-nearest and exclusion contract

All three CPU query functions accept an optional original source slot to exclude.
A deleted or absent ID excludes nothing. K-nearest returns ascending squared
distance, then ascending source ID; coincident other points remain eligible.
CPU k=0 is empty and k above the eligible count returns all eligible rows.

For recorded Vulkan queries set `KNearestCount` to 1..64, `Capacity` to the same
value and `Radius` to -1. Zero `KNearestCount` retains nearest/radius modes.
Optional `ExcludedIndices` contains one original uint32 ID per query; `~0u`
means no exclusion. Headers report the actual returned kNN count (at most k),
with unused result entries marked invalid. Radius headers still count every
eligible hit even when result capacity truncates storage.

`QueueGpuKNearest(handle, queries, k, excludedSlots, reuse)` drives the existing
frame participant and asynchronous readback. Exclusions are empty or have the
query count; unsupported sizes fail explicitly. On `Ready`, query i has
`Counts[i]` valid neighbors in its `Capacity`-wide result slice. A stale target
before recording fails the batch; an already-submitted batch retains its input
snapshot until completion. Consumers still own result-publication freshness.

`QueueGpuRadius(handle, queries, radius, capacity, excludedSlots, reuse)` uses
the same frame/readback machinery with capacity 1..1024. On `Ready`,
`Counts[i]` is the total hit count; only `min(Counts[i], Capacity)` entries
are stored, ordered by source ID. Consumers requiring complete support must
reject overflow. `GpuQueriesAvailable()` reports operational device/frame
composition; per-request validation still checks sizes, bounds and freshness.

CPU normal kernels can call `Geometry::PointCloud::Normals::Estimate(points,
index, params)`. The supplied index must match the points exactly. The adapter
preserves PCA and orientation behavior, including the existing k+1-then-filter
neighborhood policy. It does not change default method selection. The [normal-estimation workflow](normal-estimation.md) integrates canonical
config/UI/publication with cached CPU LBVH acquisition and Vulkan query chunks.
The `Estimate(points, Neighborhoods{offsets, indices}, params)` overload accepts
complete candidate rows, validates their CSR layout/indices, and reuses the
existing CPU PCA/orientation implementation. Runtime maps GPU source IDs back
to compact input rows before fitting. Radius overflow fails without publishing.
[Outlier analysis](outlier-analysis.md) uses shared kNN/exclusion and framed radius
counts through RUNTIME-209/UI-041; CPU classification publishes named mask/score
properties. Radius counts remain complete beyond retained-hit capacity. See the
[consumer inventory](spatial-index-consumers.md) for the other adapters.

For an entity consumer, query source IDs directly:

```cpp
auto acquired = cache.Acquire(world, entity, positionProperty);
if (acquired.Ready()) {
    auto neighbors = cache.KNearest(acquired.Handle, queryPosition, 16, sourceSlot);
    // nullopt means stale/invalid; a present empty vector is a valid empty result.
}
```

For a CPU method accepting a supplied index, retain a snapshot lease:

```cpp
if (auto snapshot = cache.Snapshot(acquired.Handle)) {
    auto normals = Geometry::PointCloud::Normals::Estimate(
        snapshot->Index.Points(), snapshot->Index, params);
    // Result rows are compact here. Publication maps through snapshot->Slots
    // and validates the originating source revisions before applying.
}
```

[Kernel density](kernel-density.md) reuses framed kNN candidates for nearest-other
spacing and local Gaussian support. CPU bandwidth/evaluation remains shared;
Vulkan preserves the extra candidate (k<=63) and canonical-domain publication.

[Point spacing and radii](point-spacing.md) reuse the same framed kNN cache with k+1-then-self-filter semantics. CPU reduction computes radii and nearest-other spacing from one query set; rendering these model-space radii is tracked separately by RUNTIME-222.

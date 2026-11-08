# Spatial indices

See the [consumer inventory](spatial-index-consumers.md) for current and planned
uses, missing query capabilities, ownership decisions and linked open tasks.

The Sandbox composes `Runtime.SpatialIndexCache`, a concrete runtime module
that registers itself as a service. It owns lazy entity point indices.
The class and its implementation retain matching C++ linkage so context records
can borrow it by pointer without importing the cache or LBVH API. Consumers that
invoke queries import the owning module; no second service or storage owner exists.
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
invalid queries. `PrepareQuerySession` is the reusable GPU query interface for worker-stepped
methods; kernels that traverse the index directly retain a `PrepareGpuIndex` view.
The old `QueueGpuNearest`, `QueueGpuKNearest`, `QueueGpuRadius` and
`QueueGpuNearestTransformed` batches are test-only until RUNTIME-355 removes them;
source rules prohibit production callers. The cache keeps lazy build and query recording private: it uploads live positions and the source-slot mapping, then
records whatever the target still lacks (canonical-slot gather, hierarchy build)
and the batched query into one submission. A queue never substitutes a CPU result
when the device or target is unavailable; the batch fails with a diagnostic instead.

### Submission and completion (ARCH-023)

Every query batch and every `QueueGpuCompute` is submitted inside the queue call
through `IDevice::SubmitComputeReadback`: the method compute lane on the async
compute queue, or the graphics queue where the device has none or
`render.method_compute_graphics_queue` forces it. Both use the same backend path;
the transfer queue's `ComputeLane` diagnostic reports which one ran. Nothing in
the cache records into a graphics frame, and there is no latency choice. A
request is `Submitted` once the device accepted it and `Ready` (or `Failed`) when
its own readback sink ran in the transfer queue's `CollectCompleted`, never after
a count of frames. A batch reads back one packed buffer: `queries x capacity`
neighbor rows followed by one header per query.

Producer state is a receipt, not a recording. An entry counts its gather and its
hierarchy (and `GpuBuilds`) only after a submission that recorded them was
accepted; the graphics workspace's own recorded-build flag only lets a build and
a query share one command buffer. A device refusal, before or after recording,
fails the request at once and is never replayed (a method recorder may already
have advanced a cursor): an invalid token means nothing was submitted, the entry
stays unbuilt, a late sink publishes nothing, and the next new request records
the gather and build again. Host preparation comes first, outside the recorder:
the entry's buffers, its canonical slot (`AcquireInput`, whose upload is issued on
the graphics lane then), the hierarchy storage and pipelines, and the batch's host
writes; the recorder only records the gather, build and query. Each owner
submission waits for every transfer issued before it
(`ComputeInputOrder::AfterIssuedTransfers`), so those uploads precede it on either
lane; producers recorded into a frame that has not been submitted yet are not
ordered before it. The graphics ticket also covers earlier graphics batches, so
such a submission can wait for the frame submitted before the latest upload. A
query session's fixed-target preparation is always one such submission; its
worker pages then read only prepared inputs, use `LaneOrderOnly` and wait for no
later graphics-queue upload (queue order still applies where the method lane
shares a queue with frames).

Residency slots a submission reads or writes are guarded by its returned token:
`GpuPropertyResidency::BeginSubmission`/`EndSubmission` stamp every slot noted
(`NoteUse`) or acquired while recording, besides the frame of that use. Ring
reuse, canonical replacement, discard and pruning therefore wait for that
token however many graphics frames pass, and an exhausted ring drops the
preview instead of waiting. The stamp is a safety completion; readback counters
count only transfers. Recorders must `NoteUse` every residency slot they bind
while recording (canonical inputs held by a lease are also covered by the
lease). Batches and computations stay owned by the cache, with their recorders'
leases, until their sink ran; shutdown fails what is still pending after the
device-idle wait. Rendering never waits for unfinished method compute.

`QueueGpuCompute` records a consumer callback over the cached node, compact-point
and original-slot buffers, then reads back one bounded final result. The cache
retains the index and recorder through completion and device shutdown; consumers
capture their method workspace in the recorder. The callback receives a command
context and shader-readable index view. A failed recording/readback returns a
diagnostic, and runtime consumers still validate source revisions before
publishing properties.

ARCH-023: a kernel that records on its step run's worker instead takes
`PrepareGpuIndex(handle)` at its run's Init on the owner: the same build (samples,
row map, hierarchy) through one accepted owner submission, recorded only when
missing, then an immutable `SpatialGpuIndexLease` with the resolved addresses and
the buffers a page barriers on. The lease keeps the entry and its device buffers
alive whatever the cache does meanwhile; nothing is rebuilt per step and the worker
never sees the cache. Once the run drops it, the cache stamps the residency slots it
borrows with the compute fence and frees it (with an evicted entry) after that
fence. A lease still held at shutdown survives the queue participant's shutdown
(run workers may still read it until the transactions drained); the module's final
`OnShutdown`, with workers drained and the device idle, releases its entry's device
buffers on the owner and empties its view and barriers, so a later drop frees host
memory only. `Stats().IndexDeviceBytes` counts each entry still allocated once,
including one held only by sessions or leases. The scalar, normal, outlier and
resident keypoint kernels use it.

Method workspaces come from `LeaseGpuWorkspace<T>()` instead of a fresh object per
run. Overlapping leases never share an object; a dropped lease becomes leasable
again only after the frames in flight plus one and the compute-lane fence, and the
cache keeps at most one idle workspace per kind (its pipelines only) until
device-idle shutdown. Their device buffers are leases of the method scratch cache
(`MethodScratch()`): the owner thread returns them when it parks the workspace, and
the next run of any kind takes retired buffers of the same size class and memory
kind from there ([lifetime](engine-and-feature-modules.md)). A refused submission, including a recorder that failed after recording,
releases its lease because an invalid token guarantees no submission. Scalar analysis
(density, spacing, weights), PCA/vertex/face normals and outliers lease this way;
the CPD E-step broker holds one lease across the steps and pauses of a run until it
is applied, discarded, closed or destroyed, so later steps keep its grown buffers and
(for the same target generation) uploaded target, and later runs get its pipelines.

The recorder runs inside `QueueGpuCompute` on a command buffer of the compute
lane (see above); the result is copied into a readback slot and delivered by the
next `CollectCompleted` after the device finished, typically in the same frame's
maintenance. An index that was never built, or was made stale by
`UpdateWorkspace`, is built in that same submission before the recorder runs.

A consumer owning a changing hierarchy uses `Graphics::PointLbvhWorkspace`
directly. Its borrowed buffer addresses remain valid only while the workspace
and device remain alive.

`Runtime.SpatialIndexCache.cppm` does not import the GPU workspace, command context,
device or job service. The recording callback names the command context through
its matching `extern "C++"` forward declaration and returns a lightweight RHI
buffer handle. The CPU constructor similarly borrows `WorldRegistry` without
importing the kernel registry. `SpatialCompilationLocality.QueryInterface`
enforces that closure against the configured Clang/CMake module graph. Query
callers that also record their own GPU work import
`Extrinsic.Graphics.PointLBVH` and the RHI modules themselves.

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
| Runtime owner | `SpatialIndexCache` service/module for stable geometry; `ClusteringGpuState` for resident paged k-means |
| Config/agent | Existing `sandbox.clustering` validated backend/parameter/property config selects CPU reference or Vulkan compute |
| UI | Existing K-Means method panels use that config and `ClusteringService`; Vulkan assignment uses paged brute-force distance scans |
| Publication | Cache queries return original source slots; k-means retains its existing atomic named-property publication and stale-source checks |
| End-to-end tests | `Test.SpatialIndexCache.cpp`, `Test.PointLBVHGpuSmoke.cpp`, `Test.ClusteringServiceGpuSmoke.cpp` |

K-means uses resident positions and a private paged brute-force Lloyd workspace.
Assignment scans centroid pages; Update and Reduce use fixed 64-lane double
reduction trees, with ordered page combines and pair/serial-depth submission
budgets. It does not construct a centroid LBVH. See [k-means](../methods/kmeans.md)
for budgets and parity tolerances. The existing cluster range is [1, 1024]; no
LBVH point/bounds cap applies. CPU fallback is reported when the operational
float64 device/cache is unavailable, label storage is not UInt32, or workspace/
seed preparation fails. Busy residency/output ownership, unrepresentable rows,
and failed-recording retention refuse admission; device execution failures and
stale inputs fail the run without a silent CPU retry. Requested/actual/fallback
reporting identifies the selected path.

## Construction and limits

The [method package](../../methods/geometry/point_lbvh/README.md) fixes the
Karras 2012 radix-tree formulation. The CPU oracle is an independent exhaustive
scan. The GPU builds bounds, Morton keys, sort order, topology, and node bounds
without CPU sorting or hierarchy construction. CPU and GPU quantize Morton
keys to 10 bits per axis over cubic cells sized by the largest bounds extent,
so flat inputs do not get cells stretched along their thin axis.

On the CPU, points that share a cell receive further 10-bit-per-axis Morton
digits relative to that cell's own bounds, recursively and up to nine digits.
The original compact index is the final digit, and the radix tree splits on
the common prefix of these digit strings. Clustered inputs, or a single far
outlier that compresses the rest of the cloud into a few cells, therefore
still split spatially instead of by index. This bounds CPU tree depth by 302
key bits, and CPU traversal uses a 304-entry stack. The GPU appends the index
directly after the first Morton digit, limiting its depth to 62 with a
64-entry stack; it does not refine shared cells yet, so its trees degrade on
such inputs. Query results are identical either way.

CPU and GPU traversal visit the nearer child first and prune children against
the current limit before pushing. Pruning is strict, so boxes at exactly the
limit are still visited and results, including index tie-breaks, do not depend
on visit order.

The current GPU implementation sorts the (Morton code, index) records with the
stable radix sort of `Extrinsic.Graphics.ComputeParallelPrimitives`
(GRAPHICS-148; it replaced a bitonic network with the same resulting order) and
uses independent sorted-range bound unions, not a bottom-up atomic bounds
implementation. No speed improvement is asserted. Bounds reduction starts with one workgroup;
large-input build performance is not yet characterized by the smoke workload.

Nearest ties choose the smallest source index. Radius queries are inclusive
and keep the lowest source indices in sorted order. Headers report total hits
even when capacity is zero or exceeded. GPU invalid-input headers have status
1; valid headers have status 0. Empty trees return no hits. Coordinates must
be finite and within +/-1e18; radii are finite, nonnegative and at most 1e18.
The GPU caps points/queries at 2^20 and radius capacity at 1024; the CPU tree
caps points at 2^24. Invalid CPU builds clear previous contents.

This point index does not replace the CPU median-split `Geometry.BVH`. That
tree stores tight bounds of arbitrary element boxes (faces, edge segments, or
points as degenerate boxes) and provides overlap, k-nearest and radius
queries against those boxes. `Geometry.KDTree` names the same type
(`KDTree = BVH`); despite the name it is not a space-partitioning kd-tree.
Triangle distance, ray traversal and renderer scene acceleration require
other primitives and traversal.

`Geometry.BVH` and `Geometry.Octree` reject element boxes with non-finite
coordinates or `Min > Max`; a failed build leaves an empty tree, never the
previous hierarchy. Octree overlap queries use a growable traversal stack, so
caller-chosen depth limits are not bounded by a fixed stack size.

## Consumer leases and GPU batches

ICP uses prepared query sessions; point PCA uses a retained `PrepareGpuIndex` view.
Their immutable snapshot and GPU leases keep the source entries alive until submitted work
completes. Session pages reuse their prepared buffers, while the cache owns producer/readback
ordering and device shutdown. The test-only legacy batch API also retains its entries until
completion; it is not a route for new method consumers (RUNTIME-355).
Results retain original property row IDs; CPU snapshot indices are compact and
carry an explicit `Slots` mapping.
`SpatialIndexSnapshotMatches` is the compiled comparison shared by the nine
property-processing adapters: it rejects a null snapshot and compares ordered
source slots and numeric positions exactly. Their private `AcquirePointIndex`
helper in `Runtime.GeometryProcessingOperations.RadiusRows.cpp` compiles cache
acquisition, immutable snapshot retention and matching once. Acquisition failure
leaves lease outputs untouched; mismatch retains the lease and reuse flag for
reporting. Callers retain freshness/backend gates, status mapping and diagnostics.

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

`QueueGpuKNearest(handle, queries, k, excludedSlots, reuse)` submits through the
compute lane with one asynchronous readback. Exclusions are empty or have the
query count; unsupported sizes fail explicitly. On `Ready`, query i has
`Counts[i]` valid neighbors in its `Capacity`-wide result slice. A target that is
stale at the queue call fails the batch; a submitted batch retains its input
snapshot until completion and answers for the revision it was submitted against.
Consumers still own result-publication freshness.

`QueueGpuRadius(handle, queries, radius, capacity, excludedSlots, reuse)` uses
the same submission and readback with capacity 1..1024. On `Ready`,
`Counts[i]` is the total hit count; only `min(Counts[i], Capacity)` entries
are stored, ordered by source ID. Consumers requiring complete support must
reject overflow. `GpuQueriesAvailable()` reports an operational device and the
cache's queue participant; per-request validation still checks sizes, bounds and freshness.

## GPU query strategies

Every recorded or batched GPU point query takes an explicit
`Graphics::PointQueryStrategy`; callers choose it and report it. `Lbvh` (the
default) builds and traverses the hierarchy above. `BruteForce` scans the
resident float3 samples in input order with
`PointLbvhWorkspace::RecordBruteForceQuery` (`point_brute_force_query.comp`):
no `Reserve`, build, allocation or hierarchy read. Both strategies share one
contract: the neighbor/header ABI, the float distance expression and the
ranking helpers of `point_lbvh.glsl` (`lbvhPrecedes`, `lbvhInsertNearest`,
`lbvhInsertLowestIndex`), original IDs through the optional object/row
mapping, exclusion by original ID, inclusive radius with total counts, the
k/capacity limits, and status 1 for every query when any target is invalid.
Their outputs are bit-identical on the same inputs.

Brute force is O(queries x targets) in one dispatch, so one shared check,
`Graphics::PointBruteForceRefusal`, admits it in the recorder, the cache and
every consumer. Work counts one distance plus one comparison per retained slot
(nearest 1, kNN k, radius capacity) per pair: at most 2^20 per query
(targets x (slots + 1)) and 2^24 per dispatch (queries x that). These are
engineering limits taken from the existing K-Means submission budget and the
LBVH point limit, not measured timings; a 1024-slot radius query admits at
most 1023 targets. Requests beyond them are refused with a diagnostic (ICP
falls back to `cpu_lbvh` and says why; the bilateral filter refuses before
any device check). LBVH limits are unchanged. The double-precision
method kernels (`point_lbvh_double.glsl`) and K-Means' fused paged scan keep
their own contracts.

`SpatialIndexCache` forwards the strategy on `QueueGpuNearest`,
`QueueGpuKNearest`, `QueueGpuRadius` and `QueueGpuNearestTransformed`. Both
strategies read the same resident samples (canonical slot or gathered live
rows) and the same row mapping; brute force leaves `GpuBuilds` unchanged, and
after `UpdateWorkspace` it rewrites only the points while the hierarchy stays
marked for rebuild, which the next LBVH submission (batch or `QueueGpuCompute`)
records before its query. Recording composes into a
caller's command buffer without CPU readback. There is no automatic selection
threshold: no measured comparison exists. Consumers with an explicit strategy
choice are ICP (`vulkan_brute_force`, see [registration](registration.md)) and
the bilateral filter plugin (`vulkan_brute_force`, see
[bilateral point filter](bilateral-point-filter.md)).

CPU normal kernels can call `Geometry::PointCloud::Normals::Estimate(points,
index, params)`. The supplied index must match the points exactly. The adapter
preserves PCA and orientation behavior, including the existing k+1-then-filter
neighborhood policy. It does not change default method selection. The [normal-estimation workflow](normal-estimation.md) integrates canonical
config/UI/publication with cached CPU LBVH acquisition and resident Vulkan PCA kernels.
The `Estimate(points, Neighborhoods{offsets, indices}, params)` overload accepts
complete candidate rows, validates their CSR layout/indices, and reuses the
CPU PCA/orientation implementation for supplied rows. The resident Vulkan normal
path gathers through `lbvhQueryDouble` and fits in double on the device without
downloading neighborhoods; MST is refused for that path. Radius overflow fails
without publishing.
[Outlier analysis](outlier-analysis.md) uses shared kNN/exclusion and GPU radius
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

[Kernel density](kernel-density.md) reuses GPU kNN candidates for nearest-other
spacing and local Gaussian support. CPU bandwidth/evaluation remains shared;
Vulkan preserves the extra candidate (k<=63) and canonical-domain publication.

[Point spacing and radii](point-spacing.md) reuse the same GPU kNN cache with k+1-then-self-filter semantics. CPU reduction computes radii and nearest-other spacing from one query set; rendering these model-space radii is tracked separately by RUNTIME-222.

## Private moving-point working sets

`SpatialIndexCache::CreateWorkspace` creates an owned immutable point index with identity source IDs. The returned snapshot is the caller's lifetime lease; releasing the final caller lease expires the handle, and `Prune` evicts the entry. Queued/submitted batches retain the underlying entry and GPU resources through safe completion. Calls and GPU resource retirement stay on the device-owner thread. Immutable CPU snapshots can outlive cache eviction.

A cached entity index only GPU queries read is acquired GPU-only (`Acquire(..., cpuIndex = false)`, ARCH-023: ICP's Vulkan target, the bilateral and descriptor plugins' Vulkan captures, Vulkan keypoints). The entry keeps its live rows' slots and points, keyed and revision-checked exactly like a CPU entry, and builds no CPU hierarchy; `Stats().Builds` does not count it and CPU queries answer nullopt. Its snapshot (`Cpu` false) still matches its rows and points (`SpatialIndexSnapshot::Points`). A later CPU acquisition of the same current entry builds the hierarchy once into a new snapshot. The handle, rows, revision keys, device buffers and earlier snapshot leases (and the worker sessions reading the entry) stay.

[Bilateral point filtering](bilateral-point-filter.md) uses the entity cache for its first pass and a moving `SpatialQuerySession` (no CPU hierarchy, its device LBVH rebuilt on the worker into storage prepared at the start and reused by every pass) for later Vulkan passes. Original ECS revisions independently guard publication at Accept. Lower-layer methods can still own direct geometry/graphics workspaces without runtime dependencies.

`QueueGpuNearestTransformed` retains immutable source coordinates in its reusable
query batch and uploads only a double-precision matrix when those values are
unchanged. Its transform dispatch requires shader float64; ordinary queries keep
their existing capability requirements. Transformed nearest-query batches return to the
workspace pool after completion and retirement, and their buffers to the method
scratch cache; an idle batch retains neither the target scene index nor buffers. `GpuQueryUploadBytes` counts query input writes (including
poses), separately from index-build uploads and result readbacks.

Property smoothing leases its explicit filter and implicit conjugate-gradient
workspaces from the same pool. A run takes retired scratch of sufficient size class;
each run refreshes its operator data, channel state, row map and restore mask.

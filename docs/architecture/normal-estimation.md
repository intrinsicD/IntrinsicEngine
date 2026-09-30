# Normal estimation

Open **View / Normal Estimation**, or **Mesh / Graph / PointCloud → Processing →
Vertices → Normals**. These menu entries open one shared window. Choose the
canonical position property, method and named output; then select
**Estimate normals**. The Entity input follows scene selection, including clearing
when nothing is selected; an explicit input choice lasts until selection changes.
**Show normals** selects the named output for color display through the same
undoable Appearance property configuration on the corresponding surface, edge,
or point layer. **Mesh → Processing → Faces → Normals**
opens the same window with `mesh_face_normals`, vertex positions and `f:normal`
on the face property set selected. **Show face normals** selects Face surface
appearance and displays the output as a constant color per original face.

## Method and input contract

| Method | Required inputs | Neighborhood and output |
| --- | --- | --- |
| `point_set_pca` | At least three live finite float3 samples on any resolved element domain | Existing local PCA kernel with kNN or complete radius neighborhoods; optional minimum-spanning-tree orientation |
| `mesh_face_normals` | Named mesh vertex positions, polygon face rings and halfedge topology | Normalized full-polygon area vector; one object-space float3 normal on each source face |
| `mesh_face_weighted` | Named vertex positions, polygon face rings and halfedge topology | Incident polygon normals with uniform, area, angle, area-angle or Max weighting |
| `graph_neighborhood` | Named vertex/node positions and canonical edge endpoints | Existing adjacency-based local normal kernel; mesh adjacency is accepted without creating a graph entity |

`mesh_face_weighted` and `mesh_face_normals` also run on the GPU property residency (backend `vulkan`, see
[Vulkan vertex normals](#vulkan-vertex-normals-on-the-gpu-property-residency)).

Point-set PCA accepts mesh vertex, edge, halfedge and face properties; graph
node, edge and halfedge properties; and point-cloud properties. Slot semantics
do not prescribe property names: `f:centroid` is a valid Position binding when
it contains finite float3 samples. Topology methods have stronger requirements;
the same runtime preflight supplies command validation and UI disabled reasons.
Readiness checks resolve typed inputs without rebuilding an owned mesh or graph.
Execution snapshots are constructed at submission. Their spatial support remains
incident topology, independent of the selected PCA query backend. Missing or malformed topology fails before publication.

PCA, mesh vertex weighting and graph normals write only a distinct, same-domain
float3 output property. `mesh_face_normals` reads mesh vertex positions and writes
only its named MeshFace output; face slots retain their original indices even
when the execution snapshot omits deleted faces. It normalizes the existing
`MeshUtils::FaceAreaVector` result from the full polygon ring, preserving winding.
Degenerate polygons and faces touching deleted vertices receive the configured
normalized fallback (or +Z for a zero fallback).
Existing deleted-row values are preserved; deleted slots in a newly created
output are zero. Point-set PCA excludes deleted rows, including halfedges of
deleted edges. Mesh face weighting excludes deleted faces and faces touching
deleted edges; deleted vertices retain their output values. Graph normals use
vertex/edge deletion masks. Topology, input properties, unrelated properties,
and entity provenance remain intact.

## Configuration and execution

The Sandbox section `sandbox.normal_estimation` has schema
`intrinsic.runtime.sandbox.normal_estimation`, version 1. Its payload includes
entity, method, backend, full Position/Output references, neighborhood size,
radius, orientation, fallback normal, mesh weighting, numerical tolerances and
`gpu_query_batch_size` (default 4096, range 1..16384).
For example, a payload can bind face centers as follows (omitted controls use
schema defaults):

```json
{
  "entity": 1,
  "method": "point_set_pca",
  "backend": "vulkan_lbvh",
  "gpu_query_batch_size": 4096,
  "orientation": 0,
  "positions": {"domain": "MeshFace", "name": "f:centroid", "kind": "vec3"},
  "output": {"domain": "MeshFace", "name": "f:pca_normal", "kind": "vec3"},
  "k_neighbors": 15,
  "minimum_neighbors": 2
}
```

`ApplyEditorNormalEstimationConfig` validates and previews an engine config
before applying it through the shared hot-config path.
`ApplyEditorConfiguredNormalEstimation` reads that active section. The typed
command uses the same binding/numerical preflight. Unknown-domain defaults
resolve to the entity's vertex/node/point domain; explicit domain bindings are
preserved. Invalid edits never replace the active config.

`Extrinsic.Runtime.NormalOperations` exposes commands through the generic
`EditorProcessingCommands` handle and copies results through
`PrepareEditorNormalFrame`. Its implementation runs copied CPU jobs through JobService
when composed, with a synchronous path for CPU direct/headless callers. Vulkan
requests require the framed cache, a float64-capable device and JobService. The
resident PCA kernel computes neighborhoods, covariance and normals on the device;
a terminal job waits for Accept/Discard. Workers do not wait on GPU fences. Worker code does
not access live property containers. Publication checks entity identity and the
revisions/cardinality of the consumed position, deletion, topology and output
properties. Cancellation and stale completion retain the previous output.
Unrelated property edits do not invalidate a result. Output transactions share
the editor command history and validate before initial apply, undo and redo.
The configured operation is the canonical normal API for all four algorithms,
including canonical `v:normal` output. Property watches, mutable-domain lookup,
input catalogs and config application reuse the shared compiled owners. Mesh
face-ring validation and normal snapshot reconstruction live in the compiled
`Runtime.GeometryProcessingOperations.MeshSources.cpp` owner.

Immediate calls return their result directly. A callback supplied to Apply receives
only a newly queued job's terminal outcome while attached; observing an existing
active output adds no callback. An expired attachment rejects scene reads, queued
publication and history replay before dereferencing borrowed services. Prepared
frames copy retained results; their completion and dismissal callbacks use the
session attachment epoch.

## Vulkan vertex normals on the GPU property residency

`mesh_face_weighted` and `mesh_face_normals` with backend `vulkan` (RUNTIME-296, ADR 0030
decisions 8-9) run the whole method on the device as a GPU property transaction
([property coherence](property-coherence.md#vertex-normals-runtime-296)); every other
backend value is the CPU reference for these methods, and the other methods refuse `vulkan`
(PCA keeps `vulkan_lbvh`).

- **Inputs.** The positions come from their canonical residency slot
  (`ResolveGpuPropertyInput`: uploaded once per CPU revision, shared with every GPU user).
  The face rings and the vertex->face incidences are one `uint32` bundle
  (`Graphics.VertexNormals`: face offsets, corner vertices in the reference's ring order,
  vertex offsets, `(face, corner)` incidences in ascending face order, live rows), packed
  from the reference's corner table
  (`Geometry.HalfedgeMesh.Vertices.Normals::GatherFaceCornerTable`, the same walk and skip
  rules as `Recompute`) and resident under a derived key
  (`#vertex_normal_topology`, revision = a hash of the topology and deletion watches). A
  second run on the same topology revision uploads no bundle; a topology or deletion edit
  uploads it once. Its bytes and reuse are reported (`GpuTopologyBytes`,
  `GpuTopologyReused`), as are the positions' upload bytes (`GpuInputUploadBytes`, 0 when
  the revision is resident).
- **Kernels** (`vertex_normals.comp`, double precision, `precise`): a face pass forms the
  fan area vector from the first corner and the unit normal; a vertex pass gathers the
  incident faces in the reference's face order and normalizes, so the sums round as the CPU
  reference rounds them. No float atomics; the deterministic gather replaces the scatter.
  Uniform, area and max weighting run on the device; the angle weightings need a
  double-precision `acos` the device does not have and are refused (CPU only). Deleted
  faces, rings touching deleted vertices or edges, non-finite corners and area vectors under
  the epsilon are skipped as on the CPU; a deleted vertex keeps its published bytes because
  the ring starts as a copy of the output's canonical slot (zeros for a new output).
- **Transaction.** The output ring is not observed by the renderer (vec3 rings are not
  colormap scalars or positions), so there is no viewport preview: the render block keeps the
  CPU normals until Accept. Accept reads the front back once, runs the existing undoable
  "Estimate normals" publication and binds the front as the canonical slot of the new revision;
  Discard releases the ring; a stale result (positions, topology, deletion masks or the
  output changed) can only be discarded. Batch and agent commands
  (`ApplyEditorNormalEstimationCommand`) accept automatically. The result reports
  `ActualBackend = vulkan_mesh_face_weighted`, the device's valid / fallback / processed-face
  counts and the IO counters; the panel shows Accept / Discard and the IO line.
- **Parity.** [`Test.VertexNormalsTransactionGpuSmoke.cpp`](../../tests/integration/graphics/Test.VertexNormalsTransactionGpuSmoke.cpp)
  compares the accepted rows with the CPU reference on a noisy quad/triangle grid with a
  deleted face and a deleted vertex (area and max weighting): the bound is 2e-6 per
  component, i.e. a few float ulps of a unit vector (the kernels repeat the reference's double
  arithmetic; only the float rounding of the normalized result and a last-bit difference of
  the device's `sqrt` / division can differ). The reported maximum delta is recorded as a test
  property. Contract tests
  ([`Test.NormalTransaction.cpp`](../../tests/contract/runtime/Test.NormalTransaction.cpp))
  cover Accept / Discard / stale / cancel, the deleted-row bytes and the bundle's residency
  per topology revision on a mock device; the panel test drives Accept / Discard.
- **Face normals.** `mesh_face_normals` shares the kernels, the bundle format and the
  transaction: its bundle (`#face_normal_topology`) carries the face rings of the processed
  faces (not deleted, no deleted edge on the ring) with deleted-vertex corners marked, and no
  incidences. One pass forms Newell's area vector per face in double precision in ring order
  (`MeshUtils::FaceAreaVector`) and writes the normalized vector or the fallback (a deleted
  or non-finite corner, fewer than three corners, a length not above the epsilon), exactly
  as the CPU reference. `ActualBackend = vulkan_mesh_face_normals`; the same smoke compares
  the accepted face rows with the CPU reference (measured delta 0) and checks that a second
  run uploads nothing.
- PCA uses the same transaction, with its own LBVH compute workspace (below).

## Spatial ownership and numerical limits

`cpu_kdtree` remains the default PCA query backend. Explicit `cpu_lbvh` requests
acquire a property-space snapshot from `Runtime.SpatialIndexCache`. The cache
owns rebuild/reuse decisions and original-row mappings; a copied CPU lease
supplies the existing PCA kernel. Positions are interpreted in the property's
coordinate space, without applying the entity transform. Unchanged positions
and deletion masks permit reuse across requests; changed inputs rebuild.

The CPU KD-tree and LBVH PCA queries use double squared-distance keys, inclusive
radius membership, source-index ties and the k+1-then-remove-self rule. The
`BVH` query owner exposes the double-key option; other consumers retain their
existing default. PCA accumulates the query point first, then neighbors in
(distance, index) order. Deleted rows never enter the index or covariance.
CPU LBVH accepts at most 2^24 samples and coordinates/radius within 1e18.

### Resident PCA (`vulkan_lbvh`)

`Graphics.PointNormals` reads the canonical position slot and cached LBVH through
`lbvhQueryDouble`, then accumulates the covariance in double in that same order.
It writes a float3 ring through `EditorNormalTransaction`. There are no neighborhood
downloads or CPU covariance fits. It requires an operational float64 device, at
most 2^20 live samples, coordinates/radius within 1e18, at most 64 kNN candidates
(including self), and complete radius support of at most 1024 candidates. Overflow
fails the whole result before publication. Radius queries return capacity+1 immediately
on overflow; scalar/outlier queries still count every dense neighbor. Complete rows
are heapsorted by source index, then by distance/index for PCA, preserving the
reference order. Pages clamp `gpu_query_batch_size` to 64..4096 rows for both modes (a configured
value of 1 still exercises the smallest dispatch chunk). Each page is a separate framed
compute submission; the next is queued only after completion/readback of the previous
page. Scratch is reused and diagnostics accumulate across pages; output is published
only once, after every page succeeds. Cancellation, staleness or overflow discards the
ring.

Radius queries stop at the first candidate past the 1024 capacity, so a dense overflow
(the case that timed out the device when every row of a 1026-point cluster inserted all
candidates in one submission) costs O(capacity) per row. The remaining worst case is
the same as for kNN: a query whose sphere intersects many tree boxes without enclosing
their points can visit up to 2N-1 nodes. That bound is shared with every LBVH consumer
and is not specific to radius search.

Subnormal coordinates, radius/fallback components
and double-subnormal tolerances are refused explicitly. The shader emulates
binary32 rounding in double where needed and stores subnormal output components
through integer bits, avoiding device float flush-to-zero behavior.

The primary symmetric eigensolver remains closed form. `Geometry.PCA` and
`pca_eigen_double.glsl` use matching double range-reduced atan and cosine series;
repeated or ambiguous roots use the same bounded largest-pivot Jacobi fallback.
This replaces the reference's platform libm/Eigen fallback so both implementations
have a deterministic basis and expression order. Near repeated roots, eigenvector
bases can differ from historical output. After float conversion and normalization,
valid PCA normals take the sign that makes the largest-magnitude component positive, breaking
exact magnitude ties by the lowest axis (x, then y, then z), identically on CPU
and GPU. This establishes a signed normal convention, not a global outward
orientation; the general PCA eigenframe keeps its existing handedness contract. Minimum-neighbor, collinear-ratio, degenerate-length and normalized fallback
rules remain those of the reference. CPU eigensolver tests check eigenpair
residuals and repeated roots; existing callers share the same canonical solver.

Only **unoriented** PCA is admitted on the device. MST remains available on CPU
backends and is explicitly refused for `vulkan_lbvh`; viewpoint orientation is
not currently a configuration mode. Default MST + `vulkan_lbvh` previously ran
through CPU fitting/orientation; it now fails explicitly instead of hiding that
CPU stage.
`GpuInputUploadBytes`, `GpuInputCacheHits` and `CpuStageReadbackBytes` report input
residency traffic and diagnostic/Accept downloads. The result message carries
these counters for command/agent consumers. A second resident run has no input
uploads; no per-iteration upload is needed. Legacy neighborhood/CPU fit timing
fields are zero for resident runs, not performance measurements.

| `method.engine-integration` field | Resident PCA disposition |
| --- | --- |
| Input | Canonical float3 positions on all eight point-compatible domains; deletion masks retain source rows |
| Config/agent/UI | `vulkan_lbvh`; shared preflight; Normal Estimation panel Accept/Discard; batch/agent auto-Accept |
| Publication | GPU preview: no; commit via `EditorNormalTransaction` → `GpuFrontReadback` → undoable normal publication → publication-bound `BindRevision` |
| Orientation | Unoriented on device; MST refused with a CPU-backend reason; no viewpoint mode |
| Evidence | `NormalTransaction.Pca*` contracts; `RUNTIME299PointNormalsResidency.PcaParityAcceptZeroUploadAndDiscard` smoke |

The smoke records maximum absolute delta for each component and the maximum
signed-normal angle for kNN and radius, including noisy, deleted, collinear and
coincident rows. Its proposed bound is eight float epsilons per component and
2e-6 radians: a small allowance for final binary32 normalization and device double
sqrt/division rounding. Exact or ulp-level parity is intended, but **measured GPU
deltas remain pending**; this implementation session does not execute GPU tests.

PCA is sensitive to neighborhood scale, noise, sampling density and sharp
features. Collinear, sparse or degenerate neighborhoods use the existing
fallback-normal policy. Unoriented PCA uses the largest-magnitude-positive sign rule;
MST orientation propagates signs over the neighborhood graph and does not
establish a globally outward orientation for arbitrary/disconnected geometry.

## Formulation and verification

The integration retains the existing kernels after reviewing
[Hoppe et al., 1992](https://www.hhoppe.com/proj/recon/)
(DOI `10.1145/133994.134011`) and the neighborhood-error analysis of
[Mitra, Nguyen and Guibas, 2003/2004](https://graphics.stanford.edu/~niloy/research/normal_est/normal_estimation_socg_03_ijcga_04.html).
Framework24's [point-cloud PCA system](https://github.com/intrinsicD/framework24/blob/81c54ad4294280fc034d39e46eafc1a29d598b81/lib_bcg_viewer/src/bcg_system_point_cloud_vertex_pca.cpp)
and [mesh vertex-normal system](https://github.com/intrinsicD/framework24/blob/81c54ad4294280fc034d39e46eafc1a29d598b81/lib_bcg_viewer/src/bcg_system_mesh_vertices_normals.cpp)
supply the behavioral baseline for kNN/radius and face-weighting choices.
PCA eigenvalue/features/saliency publication remains separate.
Adaptive, robust and learned estimators are excluded variants; MST orientation construction is unchanged; the portable eigensolver and double
neighbor ranking above define the current numerical reference.

[Normal workflow contracts](../../tests/contract/runtime/Test.NormalEstimation.cpp)
cover all canonical domains, same-domain publication/history, cached CPU LBVH,
radius/fallback behavior, custom-position topology methods, deletion masks,
queued staleness/cancellation, config parity and vector recipe binding. Face-normal
cases cover full polygon rings, winding, degenerate fallback, source face slots
and queued publication to the face property set. [Render extraction tests](../../tests/integration/runtime/Test.RuntimeRenderExtraction.cpp)
check that face scalar and normal-color values follow the surface's triangle map.
[Sandbox integration tests](../../tests/integration/runtime/Test.SandboxEditorPresentation.cpp)
cover the shared window aliases. Existing point-normal kernel tests and the
[point-LBVH smoke manifest](../../benchmarks/geometry/manifests/point_lbvh_knn_smoke.yaml)
provide lower-level correctness and benchmark fixtures. Runtime integration
coverage does not establish Framework24's entire PCA/feature/saliency workflow
beyond the named fixtures. [Vulkan readback tests](../../tests/integration/graphics/Test.PointLBVHGpuSmoke.cpp)
exercise kNN/radius output against CPU normals on all eight canonical domains,
cache reuse, deleted rows, cancellation/staleness, history and dense-radius
rejection. The [runtime smoke manifest](../../benchmarks/geometry/manifests/point_lbvh_normal_runtime_smoke.yaml)
records cold/warm method cost and separate CPU reference timing; this small
fixture does not establish a performance advantage.

The [2026-09-09 verification record](../../ara/evidence/tables/normal_vulkan_verification_2026-09-09.md)
binds the historical CPU and Vulkan neighborhood/CPU-fit fixtures to source
hashes and C80. The local Clang 23 `ci` gate passed 4,376 tests with one expected
unsanitized LSan-control skip; the Sandbox built. Two corrected `ci-vulkan`
ASan+UBSan cases executed on RTX 3050 (driver 590.48.01), including the eight-domain
normal workflow. Existing GPU leak-detection exclusions remain, so this does
not establish whole-process leak freedom.

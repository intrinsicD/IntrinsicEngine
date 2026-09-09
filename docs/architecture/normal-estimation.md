# Normal estimation

Open **View / Normal Estimation**, or **Mesh / Graph / PointCloud → Processing →
Vertices → Normals**. These menu entries open one shared window. Choose the
entity, canonical position property, method and named output; then select
**Estimate normals**. **Show normal vectors** binds the selected output and
position properties through the existing vector-field visualization recipe.
The visualization window owns glyph styling. **Mesh → Processing → Faces → Normals**
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

The existing geometry-processing owner runs copied CPU jobs through JobService
when composed, with a synchronous path for CPU direct/headless callers. Vulkan
requests require the framed cache and JobService: a neighborhood job advances
GPU batches/readback on the device thread, then releases a dependent CPU
PCA/orientation job. Both jobs identify the same named output. Workers do not
wait on GPU fences and fitting does not run in the UI frame. Worker code does
not access live property containers. Publication checks entity identity and the
revisions/cardinality of the consumed position, deletion, topology and output
properties. Cancellation and stale completion retain the previous output.
Unrelated property edits do not invalidate a result. Output transactions share
the editor command history and validate before initial apply, undo and redo.
Legacy default-property normal command APIs remain available to existing callers.

## Spatial ownership and numerical limits

`cpu_kdtree` remains the default PCA query backend. Explicit `cpu_lbvh` requests
acquire a property-space snapshot from `Runtime.SpatialIndexCache`. The cache
owns rebuild/reuse decisions and original-row mappings; a copied CPU lease
supplies the existing PCA kernel. Positions are interpreted in the property's
coordinate space, without applying the entity transform. Unchanged positions
and deletion masks permit reuse across requests; changed inputs rebuild.

All three query backends preserve the existing k+1-then-remove-self neighborhood rule and
complete radius support. Equal-distance boundary ties can select different
neighbors in the existing KD-tree and LBVH implementations; no universal
bitwise equivalence is asserted. The result reports requested/actual backend,
index reuse, valid/fallback counts and publication status. A missing cache or
unsupported LBVH input is rejected rather than silently changing the requested
backend. CPU LBVH accepts at most 2^24 samples and finite coordinates/radius
within 1e18. The explicit `vulkan_lbvh` option uses the same cache with framed kNN/radius
queries, followed by supplied-neighborhood CPU PCA and orientation. It requires
an operational Vulkan device, at most 2^20 live samples and coordinates/radius
within 1e18. kNN accepts at most 64 candidates including the reference kernel's
extra candidate (normally k/minimum <=63). Radius rows report the total hit
count; more than 1024 candidates fails without publishing partial normals.
Use a CPU backend or a smaller radius for such dense support. Query chunks bound
transient GPU buffers; complete neighborhoods are retained for CPU fitting and
MST orientation. The result exposes GPU batch count, elapsed neighborhood time
(including frame waits/readback), and CPU compute time. CPU compute time also
includes neighborhood search on CPU backends; it is not a device timestamp.
PCA and orientation remain CPU work. The default stays `cpu_kdtree`.

PCA is sensitive to neighborhood scale, noise, sampling density and sharp
features. Collinear, sparse or degenerate neighborhoods use the existing
fallback-normal policy. Unoriented PCA leaves eigenvector signs unconstrained;
MST orientation propagates signs over the neighborhood graph and does not
establish a globally outward orientation for arbitrary/disconnected geometry.

## Formulation and verification

The integration retains the existing kernels after reviewing
[Hoppe et al., 1992](https://www.hhoppe.com/proj/recon/)
(DOI `10.1145/133994.134011`) and the neighborhood-error analysis of
[Mitra, Nguyen and Guibas, 2003/2004](https://graphics.stanford.edu/~niloy/research/normal_est/normal_estimation_socg_03_ijcga_04.html).
Framework24's [point-cloud PCA system](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_point_cloud_vertex_pca.cpp)
and [mesh vertex-normal system](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_mesh_vertices_normals.cpp)
supply the behavioral baseline for kNN/radius and face-weighting choices.
PCA eigenvalue/features/saliency publication remains separate.
Adaptive, robust and learned estimators are excluded variants; this wiring does
not change the oracle, its orientation construction or its numerical policy.

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
binds the current CPU and actual Vulkan normal-neighborhood fixtures to source
hashes and C80. The local Clang 23 `ci` gate passed 4,376 tests with one expected
unsanitized LSan-control skip; the Sandbox built. Two corrected `ci-vulkan`
ASan+UBSan cases executed on RTX 3050 (driver 590.48.01), including the eight-domain
normal workflow. Existing GPU leak-detection exclusions remain, so this does
not establish whole-process leak freedom.

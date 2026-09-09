# Normal estimation

Open **View / Normal Estimation**, or **Mesh / Graph / PointCloud → Processing →
Vertices → Normals**. These menu entries open one shared window. Choose the
entity, canonical position property, method and named output; then select
**Estimate normals**. **Show normal vectors** binds the selected output and
position properties through the existing vector-field visualization recipe.
The visualization window owns glyph styling.

## Method and input contract

| Method | Required inputs | Neighborhood and output |
| --- | --- | --- |
| `point_set_pca` | At least three live finite float3 samples on any resolved element domain | Existing local PCA kernel with kNN or complete radius neighborhoods; optional minimum-spanning-tree orientation |
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

Every variant writes only a distinct, same-domain float3 output property.
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
radius, orientation, fallback normal, mesh weighting and numerical tolerances.
For example, a payload can bind face centers as follows (omitted controls use
schema defaults):

```json
{
  "entity": 1,
  "method": "point_set_pca",
  "backend": "cpu_lbvh",
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
when composed, with a synchronous path for direct/headless callers. Work does
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

Both paths preserve the existing k+1-then-remove-self neighborhood rule and
complete radius support. Equal-distance boundary ties can select different
neighbors in the existing KD-tree and LBVH implementations; no universal
bitwise equivalence is asserted. The result reports requested/actual backend,
index reuse, valid/fallback counts and publication status. A missing cache or
unsupported LBVH input is rejected rather than silently changing the requested
backend. CPU LBVH accepts at most 2^24 samples and finite coordinates/radius
within 1e18. PCA and orientation remain CPU work; there is no GPU normal-solve
option or performance-based default change.

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
queued staleness/cancellation, config parity and vector recipe binding.
[Sandbox integration tests](../../tests/integration/runtime/Test.SandboxEditorPresentation.cpp)
cover the shared window aliases. Existing point-normal kernel tests and the
[point-LBVH smoke manifest](../../benchmarks/geometry/manifests/point_lbvh_knn_smoke.yaml)
provide lower-level correctness and benchmark fixtures. Runtime integration
coverage does not establish Framework24's entire PCA/feature/saliency workflow
or GPU normal-estimation parity.

The 2026-09-08 local Clang 23 `ci` run passed 179 focused normal/editor checks
and the full CPU gate (4,363 passed; one expected unsanitized LSan-control skip).
The Sandbox executable also built. These runs do not supply a new GPU or
sanitizer qualification.

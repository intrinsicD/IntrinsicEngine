# Construct from points

The sandbox's **View → Construct from Points** window is also available under
**Mesh / Graph / PointCloud → Processing**. Choose a canonical float3 position
property, then Hoppe surface reconstruction or kNN graph construction. These
operations create a separate mesh or graph; they preserve the source geometry.

`sandbox.point_construction` is a version-1 app config section with schema ID
`intrinsic.runtime.sandbox.point_construction`. The editor, config documents
and programmatic callers share validation and the configured command path:
`ApplyEditorPointConstructionConfig`, `GetEditorPointConstructionConfig`,
`PreviewEditorPointConstructionCommand`, and
`ApplyEditorConfiguredPointConstruction`.

| Control | Meaning |
| --- | --- |
| `entity`, `positions`, `normals` | Render entity ID and full domain/name/vec3 property references. All eight canonical domains are supported. |
| `method` | `hoppe` or `knn_graph`. |
| `backend` | `cpu_reference`, `cpu_lbvh`, or `vulkan_lbvh`; this selects neighbor queries. |
| `output_name` | Name of the new entity. |
| `k_neighbors` | 1..63; Hoppe uses nearest for 1, otherwise k+1 weighted samples; graph construction filters a fixed k+1 row. |
| `estimate_normals`, `normal_k_neighbors` | Existing CPU normal initialization, or paired normals on the position domain. |
| `resolution`, `max_grid_vertices`, `bounding_box_padding` | Grid resolution on the longest axis, allocation limit and fractional padding. |
| `normal_agreement_power`, `kernel_sigma_scale` | Existing weighted tangent-plane field controls. |
| `mutual`, `min_distance_epsilon` | Graph reciprocity and near-duplicate filtering. |
| `gpu_query_batch_size` | Bounded query chunk size, also used to bound CPU temporary rows. |

The runtime owns composition in `Runtime.GeometryProcessingOperations`.
`Geometry.SurfaceReconstruction` owns preparation, signed-distance reduction
and extraction. `Geometry.Graph.Utils` owns row filtering, edge deduplication
and graph construction. No geometry layer imports runtime or GPU services.

CPU reference queries use the independent exhaustive float-distance/source-ID
oracle. CPU and Vulkan LBVH requests lease the existing `SpatialIndexCache`
entry for the selected property. Deleted slots are removed from captured
samples, and returned original row IDs map back to compact indices. Source
coordinates stay unchanged through normal preparation; initialization that
would filter additional samples fails instead of querying an incompatible
cached index. Reconstruction consumes bounded query chunks into one scalar
grid, rather than retaining a grid-by-k neighbor matrix. Graph candidate
storage has a separate 2^24-entry limit.

Vulkan is asynchronous and uses the existing framed query queues. The job chain
prepares samples/grid, waits for complete neighborhoods, then extracts the
geometry on CPU. No synchronous GPU fence or separate spatial service is added.
Cancellation, stale inputs, incomplete rows and unavailable GPU execution
create no entity. Requested and actual backend names are reported separately;
CPU normal initialization, field arithmetic, graph assembly, Marching Cubes,
normals and UV materialization remain CPU work.

GPU batch latency measures elapsed time from queue submission through readback,
row validation and reduction, including intervening frame waits. It is not a GPU
timestamp. CPU time includes reduction, so these two diagnostics overlap and
must not be added to estimate total operation time. The smoke harness measures
request-to-publication wall time separately.

Distance is measured in property coordinates. The output bakes the captured
full source hierarchy transform; reflection reverses mesh winding before
normal/UV materialization. New entities receive the standard render and
selection authoring recipe, local/world bounds and durable identity. Undo
removes the generated entity only while its geometry, name, transform and
hierarchy still match the generated state; redo recreates its durable identity.
Source edits after publication do not prevent undo of unchanged output.

The [method contract](../../methods/geometry/point_construction/method.yaml)
and [formulation](../../methods/geometry/point_construction/paper.md) describe
the sample-anchored field, tie semantics and numerical limits. The
[consumer inventory](spatial-index-consumers.md) tracks other spatial uses.

# FPFH descriptor analysis

**View → FPFH Descriptor Analysis** and the mesh, graph and point-cloud Processing menus share the validated `sandbox.descriptor_analysis` config and execution commands. Position and normal inputs are canonical vec3 property references. The output is 33 named float properties on that same domain, grouped into one undoable publication. The panel can display any bin with the existing scalar visualization recipe.

## Formulation

The engine preserves its existing FPFH variant: the query normal defines the Darboux frame for each noncoincident pair; alpha, phi and theta each use eleven bins. Each nonempty SPFH block is normalized to 100. FPFH adds the query SPFH and `1/k` times the inverse Euclidean-distance-weighted sum of neighbor SPFHs, then normalizes each final block to 100. Distances below `1e-9` and pairs whose connecting direction is parallel to the query normal are skipped. A block without contributions remains zero.

Support is the inclusive feature radius, excluding only the query itself. Distinct coincident points remain eligible. Neighbors are ordered by original source ID before the optional `max_neighbors` cap; this is a lowest-ID cap, not a nearest-k selection. SPFH is evaluated for every live sample before requested FPFH rows are reduced. The geometry span API preserves explicit query order and repeats; an empty query list selects every live sample. Runtime publishes every live row.

The feature radius is explicit or five times exact mean nearest-other spacing. Positive spacing is required even with an explicit radius. [Rusu, Blodow and Beetz (2009), DOI 10.1109/ROBOT.2009.5152473](https://www.cvl.iis.u-tokyo.ac.jp/class2016/2016w/papers/6.3DdataProcessing/Rusu_FPFH_ICRA2009.pdf) describes the original formulation. Its pair-source orientation chooser differs from this engine's query-normal choice. [PCL's implementation](https://pointclouds.org/documentation/fpfh_8hpp_source.html) also differs in weighting details. These outputs are not claimed to be numerically interchangeable with PCL. The histogram-resolution, orientation and CDF variants reviewed in [Szalai-Gindl/Varga (2024)](https://doi.org/10.1109/ACCESS.2024.3400591) are excluded.

## Geometry and spatial queries

`Geometry.PointCloud.Features` exposes `ResolveDescriptorScale`, span-based `ComputeDescriptors` and `ComputeDescriptorsFromNeighbors`. All use the same SPFH/FPFH reducer. `DescriptorSet` retains the resolved scale, flat row-major histogram data and source indices. Cloud wrappers compact live finite rows before indexing and remap output indices back to original slots. Invalid or deleted explicit query indices fail.

Positions must be finite and count-matched with finite nonzero normals. Invalid parameters, zero spacing and unrepresentable radii fail closed. Histogram calculations use double intermediates and float outputs; neighborhood membership uses float squared Euclidean distance. Supplied `Geometry::PointNeighborhoods` rows must contain either complete inclusive radius support or its exact lowest-ID prefix covering `min(max_neighbors, total hits)` when a cap is selected. IDs are ascending, unique and nonself. Shape, order, index range and radius membership are checked; the caller guarantees complete required support and scale provenance.

- `cpu_kdtree` streams complete radius rows twice, first for SPFH and then for requested FPFH rows.
- `cpu_lbvh` reuses an immutable selected-position index and packs complete rows or exact capped prefixes for the same reducer.
- `vulkan_lbvh` resolves scale on CPU, collects the required radius support through framed GPU jobs, then reduces SPFH/FPFH on CPU. Original IDs are remapped to compact live IDs and sorted.

No backend silently falls back. Vulkan capacity is 1..1024 hits per row and batch size 1..16384. The radius query retains lowest source IDs, so a selected neighbor cap within that capacity is supported even when total radius occupancy exceeds capacity. Uncapped overflow, or a cap larger than capacity with insufficient retained hits, rejects the operation. It never substitutes an arbitrary or distance-ranked prefix. Vulkan allows up to 2^20 live samples, CPU LBVH up to 2^24; coordinates and resolved radii must be within 1e18. Packed uint32 support must fit its offset range. Indexed paths retain O(total support) host memory; dense support may be quadratic. The reference streams rows and retains O(sample count) SPFH storage.

Matching these descriptors uses 33-dimensional histogram distance. A 3D position LBVH cannot replace descriptor-space matching.

## Configuration and publication

The version-1 section schema is `intrinsic.runtime.sandbox.descriptor_analysis`. Its fields are `entity`, `backend`, `positions`, `normals`, `outputs`, `feature_radius`, `max_neighbors`, `gpu_query_batch_size` and `gpu_radius_capacity`. Defaults use `cpu_kdtree`, automatic radius, unlimited neighbors, batch 4096 and capacity 256. `outputs` is an array of exactly 33 canonical float references in alpha/phi/theta block order. `MakeDescriptorOutputProperties(domain, prefix)` produces names such as `fpfh.alpha0` through `fpfh.theta10`; the panel's prefix control uses this same helper. Each reference can also be configured independently. Unknown fields, duplicate outputs, input/output aliasing, incompatible kinds/domains and invalid limits are rejected before apply.

All eight canonical domains are accepted: mesh vertices/edges/halfedges/faces, graph nodes/edges/halfedges and point-cloud points. Halfedges use their paired edge deletion mask. Deleted output rows retain existing values or initialize to zero for new properties. The operation preserves unrelated properties and topology. Position, normal, deletion and every output revision guard publication and history. Stale, cancelled and rejected job chains retain the previous columns. No entity-owned transient descriptor component is needed.

Results report requested/actual backend, slot/live/written counts, resolved spacing/radius, cache reuse, GPU batches, largest indexed support and CPU/GPU elapsed time. Largest support is zero on the streaming reference path. CPU timing includes Vulkan scale preparation. GPU timing covers framed neighborhood collection; it is not a kernel-only measurement.

## Verification entry points

`Test.DescriptorAnalysis.cpp` covers analytic plane bins, complete exhaustive support, caps, duplicates, query ordering, malformed rows and normal validity. `Test.DescriptorAnalysisOperations.cpp` covers canonical domains, config, all-column history/publication, visualization and stale/cancelled work. `PointLBVHGpuSmoke.Descriptor*` compares all bins across all eight domains at explicit and automatic radii, then tests stale normals, cancellation with reaping, uncapped dense radius overflow and partial submission rejection. A separate dense fixture verifies cap=1 with more than 1024 radius neighbors and all-column history.

`geometry.point_lbvh.descriptor_runtime_smoke` measures eight-domain requests through publication after one warmup; `INTRINSIC_DESCRIPTOR_BENCHMARK_OUTPUT` writes raw diagnostics. Debug smoke timings do not establish a performance improvement. [RUNTIME-226](../../tasks/active/RUNTIME-226-fpfh-descriptor-spatial-backends.md) tracks verification and review.

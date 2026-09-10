# Compact density weights

**View → Compact Density Weights** and the mesh, graph and point-cloud Processing menus share the validated `sandbox.density_weights` config and execution commands. A canonical vec3 position property produces one named float property on the same element domain. The panel uses the existing scalar visualization recipe to show the weights.

## Formulation and support

`Geometry.PointCloud.Kernels` preserves its discrete compact-support kernels. With Euclidean distance `r` and positive support radius `h`, direct weight is `1 + sum K(r,h)` over other live samples with strict `r < h`; reciprocal mode returns its inverse. Gaussian uses sigma `h/4`, ThetaLop uses `exp(-16*r*r/(h*h))`, and WendlandC2 uses `(1-r/h)^4*(1+4*r/h)`. Coincident distinct samples contribute; a single isolated sample has weight one. Distance, kernel evaluation and ordered accumulation use double precision; publication uses float.

This is the existing discrete density correction, with lineage and formulation boundaries documented in the [LOP intake](../../methods/geometry/locally_optimal_projection/paper.md) and [continuous LOP intake](../../methods/geometry/continuous_lop/paper.md). It does not introduce continuous mixture attraction or replace the separate local Gaussian density estimator.

`ComputeDensityWeightsFromNeighbors` consumes complete candidate rows through `Geometry::PointNeighborhoods`. Rows are ascending unique source IDs with valid CSR shape. Self IDs and conservative shell candidates are allowed and ignored by the reducer when appropriate. The caller guarantees all strict-support contributors are present. Invalid rows fail without weights.

The KD-tree and indexed adapters query a conservative float radius derived in double as `sqrt(h*h*(1+8*float_epsilon)+8*float_min_normal)`, rounded outward. The double kernel then determines exact support. CPU radius conversion saturates at floatmax before conversion. This avoids losing valid contributors when float squared-distance arithmetic underflows near a tiny support boundary. The same broad-phase helper serves projection methods; anisotropic attraction checks double support before using a float offset, and repulsion skips a zero radial weight before evaluating its derivative.

## Backends and ownership

- `cpu_kdtree` streams radius candidates through the existing kernel API, which also retains supplied-index/scratch overloads.
- `cpu_lbvh` reuses the selected-property index in `SpatialIndexCache` and supplies complete candidate rows to the same reducer.
- `vulkan_lbvh` gathers complete radius rows through framed GPU jobs, then runs that reducer on CPU. GPU neighbor batches are remapped from original source IDs to live compact IDs.

A private plain radius-row state and free advancement function are shared by density weights, keypoints and descriptors. Callers own their numerical support rules, job lifetimes and revision checks. This introduces no additional service or lifecycle module.

Vulkan capacity is 1..1024 hits per row and batch size 1..16384. Density weights require complete support: overflow fails without publication, even when omitted hits might ultimately fall outside strict support. There is no estimator cap or silent fallback. CPU LBVH supports at most 2^24 samples and Vulkan at most 2^20. Coordinates and the expanded query radius must stay within 1e18. A one-sample Vulkan request uses physical capacity one and accepts its empty nonself row.

The Vulkan density adapter rejects nonzero subnormal coordinate components explicitly. Zero, negative zero and normal coordinates are accepted, including tiny positive double support radii. This bounds the conservative-radius argument to the existing shader's arithmetic without assuming optional device denormal preservation. The general LBVH subnormal-coordinate contract remains a separate audit item. The [Vulkan SPIR-V environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html) permits implementation-dependent denormal handling, and [GLSL.std.450](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html) requires ordered bounds for FClamp.

Indexed paths retain all candidate rows in host memory, which may be quadratic for dense support. Packed rows must fit uint32 offsets. Query expansion can increase candidates; correctness takes priority over silently dropping contributors.

## Configuration and publication

The version-1 schema is `intrinsic.runtime.sandbox.density_weights`. Fields are `entity`, `backend`, `positions`, `weights`, `support_radius`, `kernel`, `mode`, `gpu_query_batch_size` and `gpu_radius_capacity`. Defaults are `cpu_kdtree`, support radius 1, `theta_lop`, `direct`, batch 4096 and capacity 256. Positive finite support radii up to floatmax are accepted by the reference; indexed paths apply their additional range checks. Unknown fields, invalid enum tokens, property aliasing, incompatible domains/kinds and invalid limits fail validation.

All eight canonical domains are accepted: mesh vertices/edges/halfedges/faces, graph nodes/edges/halfedges and point-cloud points. Halfedges use paired edge deletion masks. Finite live positions are required; deleted nonfinite rows are excluded. Existing deleted output rows are preserved; newly created deleted rows initialize to zero. Position, deletion and output revisions guard one atomic history operation. Unrelated properties and topology remain intact. Stale, cancelled and partially rejected job chains preserve output/history.

Results report requested/actual backend, slot/live/written counts, kernel diagnostics, minimum/maximum published weight, expanded query radius, cache reuse, largest indexed candidate count, query batches and CPU/GPU elapsed time. The streaming reference reports no indexed maximum. GPU timing covers framed candidate collection, while kernel reduction remains CPU work.

## Verification entry points

`Test.PointCloudKernels.cpp` covers analytic contributions, supplied rows, malformed support, extreme radii and tiny-radius internal-node regression. `Test.PointCloudConsolidation.cpp` checks widened candidate shells, tiny support and derivative overflow guards. `Test.DensityWeightOperations.cpp` covers canonical domains, strict config, CPU/cache agreement, deleted/unrelated preservation, history and stale/cancelled work. `PointLBVHGpuSmoke.DensityWeight*` exercises all kernel/mode combinations, stale/cancel/overflow/partial-submission failures, subnormal-coordinate rejection, tiny support and a single sample. Existing keypoint and descriptor GPU tests cover shared pagination behavior.

`geometry.point_lbvh.density_weight_runtime_smoke` measures eight-domain requests through publication after one warmup. `INTRINSIC_DENSITY_WEIGHT_BENCHMARK_OUTPUT` writes raw diagnostics. Debug smoke does not establish a performance improvement. [RUNTIME-227](../../tasks/active/RUNTIME-227-compact-density-weight-spatial-backends.md) owns verification and review.

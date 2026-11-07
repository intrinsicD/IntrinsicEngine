# Compact density weights

**View → Processing → Sampling & Density → Compact Density Weights** computes a named weight property on the selected
position domain. It is the plugin `plugins/density_weights` (ARCH-022 Q90), which owns its config section
(`sandbox.density_weights`, payload unchanged), its MCP tools `density_weights` and `density_weights_preview` and its
window; the point-cloud editing service does the capture, the GPU run and the publication. A canonical vec3 position
property produces one named scalar property (any scalar storage on the CPU, float on Vulkan) on the same element
domain. **Compute compact weights** runs the applied settings (a Vulkan run first shows its preview, published by
**Accept** and released by **Discard**; the tool accepts at once); **Show weights** uses the scalar visualization
recipe.

## Formulation and support

`Geometry.PointCloud.Kernels` preserves its discrete compact-support kernels. With Euclidean distance `r` and positive support radius `h`, direct weight is `1 + sum K(r,h)` over other live samples with strict `r < h`; reciprocal mode returns its inverse. Gaussian uses sigma `h/4`, ThetaLop uses `exp(-16*r*r/(h*h))`, and WendlandC2 uses `(1-r/h)^4*(1+4*r/h)`. Coincident distinct samples contribute; a single isolated sample has weight one. Distance, kernel evaluation and ordered accumulation use double precision; publication uses float.

This is the existing discrete density correction, with lineage and formulation boundaries documented in the [LOP intake](../../methods/geometry/locally_optimal_projection/paper.md) and [continuous LOP intake](../../methods/geometry/continuous_lop/paper.md). It does not introduce continuous mixture attraction or replace the separate local Gaussian density estimator.

`ComputeDensityWeightsFromNeighbors` consumes complete candidate rows through `Geometry::PointNeighborhoods`. Rows are ascending unique source IDs with valid CSR shape. Self IDs and conservative shell candidates are allowed and ignored by the reducer when appropriate. The caller guarantees all strict-support contributors are present. Invalid rows fail without weights.

The KD-tree and indexed adapters query a conservative float radius derived in double as `sqrt(h*h*(1+8*float_epsilon)+8*float_min_normal)`, rounded outward. The double kernel then determines exact support. CPU radius conversion saturates at floatmax before conversion. This avoids losing valid contributors when float squared-distance arithmetic underflows near a tiny support boundary. The same broad-phase helper serves projection methods; anisotropic attraction checks double support before using a float offset, and repulsion skips a zero radial weight before evaluating its derivative.

## Backends and ownership

- `cpu_kdtree` streams radius candidates through the existing kernel API, which also retains supplied-index/scratch overloads.
- `cpu_lbvh` reuses the selected-property index in `SpatialIndexCache` and supplies complete candidate rows to the same reducer.
- `vulkan_lbvh` gathers complete radius rows and evaluates strict support and double kernel sums on the device through `Graphics.PointScalarAnalysis`. Original source IDs retain compact-order semantics without neighborhood download.

The shared shader `lbvhQueryDouble` supplies ascending source IDs for fixed-order double reductions. Gaussian and ThetaLop reuse the double exponential from property filtering (`include/exp_double.glsl`); WendlandC2 uses double polynomial arithmetic. The plugin's CPU LBVH rows are the cached index's complete radius rows at the conservative query radius (self excluded, double distances, ascending), as the removed engine operation queried them. The scalar GPU methods share `Runtime.PointScalarTransaction` through the editing service's GPU scalar analysis (method 2).

Vulkan capacity is 1..1024 hits per row and batch size 1..16384. Density weights require complete support: overflow fails without publication, even when omitted hits might ultimately fall outside strict support. The overflow message names the support radius as too large, reports the largest candidate count `N` and capacity `C`, and suggests about `R*C/N`, 0 for automatic, or a CPU backend. There is no estimator cap or silent fallback. CPU LBVH supports at most 2^24 samples and Vulkan at most 2^20. Coordinates and the expanded query radius must stay within 1e18. A one-sample Vulkan request uses physical capacity one and accepts its empty nonself row.

The Vulkan density adapter rejects nonzero subnormal coordinate components explicitly. Zero, negative zero and normal coordinates are accepted, including tiny positive double support radii. A double-subnormal support is
encoded as zero for device evaluation: distinct float coordinates are necessarily
outside it, and coincident samples contribute exactly one before the support
comparison. No support-square or subnormal division is evaluated. This bounds the conservative-radius argument to the existing shader's arithmetic without assuming optional device denormal preservation. The general LBVH subnormal-coordinate contract remains a separate audit item. The [Vulkan SPIR-V environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html) permits implementation-dependent denormal handling, and [GLSL.std.450](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html) requires ordered bounds for FClamp.

CPU LBVH retains candidate rows in host memory. Vulkan retains at most 2^24 candidate entries on the device and admits at most 2^24 property rows. Non-float output storage is refused explicitly; CPU remains available for its checked scalar conversions. Shader float64 is required. Query expansion can increase candidates; correctness takes priority over silently dropping contributors.

## Configuration and publication

The plugin's section is derived from its bound rows (`Extrinsic.Runtime.DerivedConfigSections`) and writes the payload
the removed hand-written codec wrote. Its texts are the codec's where one field is wrong (backend, kernel, mode, the
capacity pair, distinct names on one domain); a wrong reference kind or domain answers as every derived section does
("positions needs a canonical typed property reference.", "Unknown element domain.") instead of the codec's shared
binding text; a missing entity answers "Density-weight target entity is stale or missing." and a mismatched output
"Density-weight outputs must be …" (the plugin's noun, where the operation said "Density-weight target is stale or
missing." and "Weight outputs …"; a CPU run's own capture answers an output that would replace a topology or deletion
property with the editing service's "The outputs cannot replace topology or deletion properties.", the readiness with
"Density-weight outputs cannot replace topology/deletion properties."), and a payload with several wrong fields reports the first in the derived order (integers, tokens,
floats, references, the cross-field check).

The version-1 schema is `intrinsic.runtime.sandbox.density_weights`. Fields are `entity`, `backend`, `positions`, `weights`, `support_radius`, `kernel`, `mode`, `gpu_query_batch_size` and `gpu_radius_capacity`. Defaults are `cpu_kdtree`, support radius 0 (automatic), `theta_lop`, `direct`, batch 4096 and capacity 256. Support radius 0 or positive finite values up to floatmax are accepted; indexed paths apply their additional range checks.

Support radius 0 derives the radius from the captured live positions through `Geometry.SupportRadius` with the LOP-family row of the [support-radius policy](support-radius-policy.md) (16th neighbour, P75, factor 1.25; backoff to rank 4). The workload budget is `gpu_radius_capacity` for every backend, so the derived radius does not depend on the backend. The rule is the same on all eight domains and scales with the model. Too few live samples for the analysis, a zero-extent set or an over-budget minimum rank fails with `InvalidProcessingParameters` and "Automatic support radius failed (status); set a positive support radius." The budget is checked on at most 2048 sampled rows, so it is not a guarantee: on Vulkan a denser unsampled row can still overflow and fail. CPU backends resolve on the job worker; Vulkan resolves on the frame thread because its dispatch starts there, after the radius-independent admission (point count, neighbour workspace, device; the service's readiness at query radius 0) and before the admission of the resolved radius (three captures: the admission on a fresh copy, the positions for the analysis, the run's own). Readiness does not resolve an automatic radius. Positive values are used unchanged in source units (RUNTIME-291). Unknown fields, invalid enum tokens, property aliasing, incompatible domains/kinds and invalid limits fail validation.

All eight canonical domains are accepted: mesh vertices/edges/halfedges/faces, graph nodes/edges/halfedges and point-cloud points. Halfedges use paired edge deletion masks. Finite live positions are required; deleted nonfinite rows are excluded. Existing deleted output rows are preserved; newly created deleted rows initialize to zero. Position, deletion and output revisions guard one atomic history operation. Unrelated properties and topology remain intact. Stale, cancelled and partially rejected job chains preserve output/history.

Results report requested/actual backend, slot/live/written counts, kernel diagnostics, minimum/maximum published weight, the resolved support radius (also appended to the CPU completion message), expanded query radius, cache reuse, largest indexed candidate count, query batches and CPU/GPU elapsed time. The streaming reference reports no indexed maximum. The resident path reports one GPU submission, zero CPU compute time and explicit residency upload/hit/Accept-readback counters in results and agent messages. The persisted query batch size does not paginate this submission.

## Verification entry points

`Test.PointCloudKernels.cpp` covers analytic contributions, supplied rows, malformed support, extreme radii and tiny-radius internal-node regression. `Test.PointCloudConsolidation.cpp` checks widened candidate shells, tiny support and derivative overflow guards. `Test.DensityWeightsPlugin.cpp` covers the section (name, payload, bounds, texts), the tools and the Jobs window row, canonical domains with every kernel and mode, CPU/cache agreement, bitwise equality with the geometry functions, deleted/unrelated preservation, history and replaced storage, stale/cancelled work, checked scalar storage, the automatic radius (mesh vertices, graph nodes and point clouds, both CPU backends, x1000 scaling), the Vulkan admission texts (refused before the automatic radius is derived) and the window. `DensityWeightsPluginGpuSmoke` exercises all kernel/mode combinations on eight domains, an automatic radius equal to the CPU's, Accept/Discard, stale/cancel/overflow (including the suggested radius), subnormal-coordinate rejection, tiny support, a single sample and an unload. A rejected Accept submission is `PointScalarTransaction`'s. Existing keypoint and descriptor GPU tests cover shared pagination behavior.

`geometry.point_lbvh.density_weight_runtime_smoke` measures eight-domain requests through publication after one warmup. `INTRINSIC_DENSITY_WEIGHT_BENCHMARK_OUTPUT` writes raw diagnostics. Debug smoke does not establish a performance improvement. [RUNTIME-227](../../tasks/done/RUNTIME-227-compact-density-weight-spatial-backends.md) owns verification and review.

## Resident scalar transaction

| method.engine-integration | Publication |
| --- | --- |
| Density weights | GPU preview: yes; commit via `GpuFrontReadback` → `PublishPointScalarField` → `BindRevision(key, revision, publication)`. |

Accept reads the float ring once and publishes through existing history; Discard
leaves CPU rows unchanged. Input/deletion/output watches guard publication,
detach discards, and batch/agent execution auto-accepts. The colormap observes
the pending scalar front. Deleted output rows preserve their base bytes.

`PointScalarTransaction` mock contracts run on a test transaction of method 2.
`RUNTIME298PointScalarResidency.ParityResidentSecondRunAndDiscard` covers all
three kernels in direct and reciprocal modes, a deleted row, second-run zero
upload and Discard. It records maximum absolute delta per variant; 2e-5 bounds
final float rounding on the O(1) fixture (existing multi-domain bounds remain
1e-5). GPU execution is pending; no measured parity for this port is claimed.

### Double distance decisions

CPU/GPU reducers promote coordinates before subtraction and disable contraction.
CPU LBVH and resident GPU queries prune, filter and cap candidates using double
squares; radius rows remain in source-index order. The kd-tree keeps its float
broad phase: 8*float-epsilon relative slack plus 8*float-min-normal absolute slack,
with outward radius rounding, bounds distance/radius rounding and FTZ errors.
The existing double strict-support predicate filters the conservative shell.
For h<1e-100 the expansion omits h squared, whose contribution cannot affect the
rounded radius at this floor, avoiding double underflow. Vulkan refuses non-normal double
support radii; the CPU still handles tiny radii through exact coincident/outside
support branches without forming their squares. Gaussian/ThetaLop exponents lie in [-16,0]; the shared
[exponential accuracy contract](kernel-density.md#numerical-query-and-exponential-contract)
applies. Float output properties and candidate-overflow failure remain unchanged.

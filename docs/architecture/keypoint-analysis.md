# ISS-style keypoint analysis

**View → ISS Keypoint Analysis**, also exposed by mesh, graph and point-cloud Processing menus, publishes a named uint32 keypoint mask and float saliency on the selected position property's domain. The same validated config and execution commands serve UI and config/agent callers. Mask and saliency display use existing label and scalar visualization recipes.

## Formulation

The detector preserves the engine's centroid-PCA variant: each covariance includes the center and every other live point in the inclusive salient radius, ordered by source index. Descending eigenvalues must have positive lambda1 and lambda2, with lambda2/lambda1 <= gamma21 and lambda3/lambda2 <= gamma32. Candidate saliency is lambda3. Inclusive-radius nonmaximum suppression rejects lower scores and resolves equal scores by lowest original source index. A valid zero-saliency maximum can be selected; rejected candidates also have zero saliency, so use the mask to identify retained keypoints.

Automatic salient and suppression radii are respectively 6 and 4 times exact mean nearest-other spacing. Positive spacing is required even when both radii are explicit, preserving the existing detector contract. Coincident peers remain eligible neighbors. Deleted Cloud slots are compacted before nearest-neighbor queries: this corrects the old eight-raw-candidate cutoff that could hide all live neighbors. The shared spacing correction also applies to descriptor automatic radii; it is not limited to keypoint detection.

[Zhong (2009), DOI 10.1109/ICCVW.2009.5457637](https://doi.org/10.1109/ICCVW.2009.5457637) describes density-weighted, query-centered scatter and reference frames. This engine variant follows the centroid covariance convention also used by [Open3D](https://www.open3d.org/docs/latest/tutorial/geometry/iss_keypoint_detector.html); [PCL's implementation](https://pointclouds.org/documentation/iss__3d_8hpp_source.html) uses query-centered scatter. This API does not implement the complete ISS descriptor. Framework24 scalar Gaussian saliency is a separate method.

## Geometry and spatial queries

`Geometry.PointCloud.Features` exposes position-span scale resolution and analysis, plus `AnalyzeKeypointsFromNeighbors`. `Geometry.SpatialQueries::PointNeighborhoods` is a non-owning offsets/indices view also used by normal estimation through its existing `Neighborhoods` alias. Consumers retain their own neighborhood policy.

Supplied keypoint rows must contain complete support at the larger resolved radius, with ascending unique nonself IDs. The reducer validates shape, range, ordering and radius membership; the caller guarantees completeness and that the scale belongs to the same inputs. Geometry uses float squared distances and the existing double covariance accumulation with float eigenvalue publication. Invalid parameters, nonfinite positions, zero spacing or unrepresentable radii/covariance fail without publishing partial results. Cloud wrappers preserve original live-slot ordering.

- `cpu_kdtree` streams complete KD-tree radius rows through the reference reducer.
- `cpu_lbvh` reuses the selected property's immutable cached index and packs complete rows for the same reducer.
- `vulkan_lbvh` resolves scale on CPU, queries complete radius support through framed GPU jobs, then computes covariance and suppression on CPU. Returned original IDs are remapped to compact live IDs and sorted. No backend fallback occurs.

Vulkan radius capacity is 1..1024 and query batch size 1..16384. Any row exceeding capacity rejects the entire operation and retains previous outputs; it never uses truncated covariance or suppression support. Vulkan supports up to 2^20 live points, CPU LBVH up to 2^24; LBVH coordinates and resolved radii are bounded by 1e18. Packed support uses uint32 offsets and fails if total retained support exceeds that range. The indexed reducers retain O(total neighborhood support) host memory, potentially quadratic for dense CPU neighborhoods. GPU query buffers are bounded by batch size times capacity.

## Runtime and config

Every canonical point-compatible domain is accepted: mesh vertices/edges/halfedges/faces, graph nodes/edges/halfedges and point-cloud points. Runtime compacts finite live samples, uses paired edge deletion for halfedges, and preserves deleted output rows, unrelated properties and topology. New output properties initialize deleted rows to zero. Input, deletion and both output revisions guard one atomic undoable publication. Cancelled or stale scale/query/reduction jobs cannot publish later. Shared private property-watch and domain helpers also serve outlier analysis and bilateral filtering.

Section `sandbox.keypoint_analysis`, schema `intrinsic.runtime.sandbox.keypoint_analysis`, version 1:

```json
{
  "entity": 0,
  "backend": "cpu_kdtree",
  "positions": {"domain":"unknown", "name":"v:position", "kind":"vec3"},
  "mask": {"domain":"unknown", "name":"keypoint_mask", "kind":"uint32"},
  "score": {"domain":"unknown", "name":"keypoint_saliency", "kind":"float"},
  "minimum_neighbors": 5,
  "salient_radius": 0,
  "nonmax_radius": 0,
  "gamma21": 0.975,
  "gamma32": 0.975,
  "gpu_query_batch_size": 4096,
  "gpu_radius_capacity": 256
}
```

Unknown domain resolves to the primary point domain. Outputs must be distinct, count-matched typed properties on the input domain and cannot overwrite position, topology or deletion data. Results report requested/actual backend, live/total/written/keypoint counts, resolved scale, index reuse, GPU batches, largest indexed support and CPU/GPU elapsed times. Largest indexed support is zero on the streaming reference path; it is not a count of selected keypoints. CPU timing includes scale preparation for Vulkan requests.

## Verification entry points

`Test.KeypointAnalysis.cpp` covers analytic scores, equal-score suppression, malformed supplied rows, independent exhaustive support and nearest-live spacing. `Test.KeypointAnalysisOperations.cpp` covers all canonical domains, shared config, history, visualization and stale/cancelled jobs. Two `PointLBVHGpuSmoke.Keypoint*` cases compare masks/scores at explicit and automatic radii, then separately exercise stale input, cancellation with reaping, radius overflow and partial-chain submission rejection.

`geometry.point_lbvh.keypoint_runtime_smoke` measures eight-domain requests through publication, with one cold warmup and one measured request set. `INTRINSIC_KEYPOINT_BENCHMARK_OUTPUT` writes raw diagnostics. Debug smoke times do not establish a performance improvement. [RUNTIME-225](../../tasks/active/RUNTIME-225-iss-keypoint-spatial-backends.md) owns verification and terminal review.

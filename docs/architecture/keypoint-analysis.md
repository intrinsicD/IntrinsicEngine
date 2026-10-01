# ISS-style keypoint analysis

**View → ISS Keypoint Analysis**, also exposed by mesh, graph and point-cloud Processing menus, publishes a named uint32 keypoint mask and float saliency on the selected position property's domain. The same validated config and execution commands serve UI and config/agent callers. Mask and saliency display use existing label and scalar visualization recipes.

## Formulation

The detector preserves the engine's centroid-PCA variant: each covariance includes the center and every other live point in the inclusive salient radius. CPU accumulation follows source-index order; Vulkan accumulation follows the cached LBVH traversal order. Descending eigenvalues must have positive lambda1 and lambda2, with lambda2/lambda1 <= gamma21 and lambda3/lambda2 <= gamma32. Candidate saliency is lambda3. Numerical residuals at or below `64 * double-epsilon * lambda1` are treated as zero before the ratio test and suppression, for both smaller eigenvalues, so planar/collinear eigensolver roundoff cannot invent candidates or saliency or break zero-score ties. Inclusive-radius nonmaximum suppression rejects lower scores and resolves equal scores by lowest original source index. A valid zero-saliency maximum can be selected; rejected candidates also have zero saliency, so use the mask to identify retained keypoints.

Automatic salient and suppression radii are respectively 6 and 4 times exact mean nearest-other spacing. Positive spacing is required even when both radii are explicit, preserving the existing detector contract. Coincident peers remain eligible neighbors. Deleted Cloud slots are compacted before nearest-neighbor queries: this corrects the old eight-raw-candidate cutoff that could hide all live neighbors. The shared spacing correction also applies to descriptor automatic radii; it is not limited to keypoint detection.

[Zhong (2009), DOI 10.1109/ICCVW.2009.5457637](https://doi.org/10.1109/ICCVW.2009.5457637) describes density-weighted, query-centered scatter and reference frames. This engine variant follows the centroid covariance convention also used by [Open3D](https://www.open3d.org/docs/latest/tutorial/geometry/iss_keypoint_detector.html); [PCL's implementation](https://pointclouds.org/documentation/iss__3d_8hpp_source.html) uses query-centered scatter. This API does not implement the complete ISS descriptor. Framework24 scalar Gaussian saliency is a separate method.

## Geometry and spatial queries

`Geometry.PointCloud.Features` exposes position-span scale resolution and analysis, plus `AnalyzeKeypointsFromNeighbors`. `Geometry.SpatialQueries::PointNeighborhoods` is a non-owning offsets/indices view also used by normal estimation through its existing `Neighborhoods` alias. Consumers retain their own neighborhood policy.

Supplied keypoint rows must contain complete support at the larger resolved radius, with ascending unique nonself IDs. The reducer validates shape, range, ordering and radius membership; the caller guarantees completeness and that the scale belongs to the same inputs. Radius membership and nearest-other distances retain the canonical float arithmetic; spacing sums and covariance accumulation use double with float eigenvalue publication. The device rounds each distance operation to float, including subnormal intermediates, so inclusive boundary neighbors agree with CPU queries. Subnormal public float values are decoded/stored through integer bits in the device kernel; tiny LBVH bounds are widened conservatively and leaf membership reads the canonical coordinates. Invalid parameters, nonfinite positions, zero spacing or unrepresentable radii/covariance fail without publishing partial results. Cloud wrappers preserve original live-slot ordering.

- `cpu_kdtree` streams complete KD-tree radius rows through the reference reducer.
- `cpu_lbvh` reuses the selected property's immutable cached index and packs complete rows for the same reducer.
- `vulkan_lbvh` resolves scale on CPU, queries complete radius support through framed GPU jobs, then computes covariance and suppression on CPU. Returned original IDs are remapped to compact live IDs and sorted.
- `vulkan_compute` uses `vulkan.keypoints.resident.paged.v1`: resident stride-12 positions, resumable nearest-other spacing, parallel double spacing reductions, centroid covariance/eigenvalue scoring, suppression against immutable candidate scores, then one score/mask ring copy. It requires shader float64; no backend silently falls back. Requested/actual backend, implementation ID, input uploads/cache hits, CPU-stage upload/readback bytes and submission counts accompany results and `run_keypoint_analysis` agent output.

The UI selects CPU or Vulkan computation. CPU exposes KD-tree, cached CPU LBVH and Vulkan LBVH neighborhood acceleration separately. `Graphics.PointKeypoints` owns the method pipeline and linear scratch/output storage. `SpatialIndexCache::QueueGpuCompute` retains the source index and caller-owned recorder until the existing frame participant completes the final readback; runtime retains revision validation and atomic history publication. A CPU job record provides the completion gate; the calculation executes through the GPU frame participant, not that worker.

Hybrid Vulkan radius capacity is 1..1024 and query batch size 1..16384. Hybrid overflow rejects incomplete support. Full Vulkan does not allocate neighbor lists or use radius capacity; it traverses salient support for both covariance passes and nonmax support for suppression. MaximumNeighbors diagnoses salient support without rejecting or restarting. This adds no point admission cap. Existing Vulkan and CPU LBVH bounds remain 2^20 and 2^24 live points, respectively; coordinates/radii remain within 1e18.

Full Vulkan traversal submissions contain at most 16,384 rows, sized by a 2^24 row/visit pair budget, each performing at most 1,024 node visits or traversal transitions. With one 1,024-visit page per row, each traversal stage at 2^20 points needs 64 submissions (three stages: 192), plus three submissions for spacing partials, scale reduction and ring copy. Longer traversals resume within the same work bounds. Per-invocation serial work is bounded by 1,024 traversal steps plus the fixed 24-step eigensolver and small fixed matrix operations. Stack, covariance, sums and traversal phase persist in a 384-byte state per active page row (at most 6 MiB, reused after each page). Dense rows continue after an immediate completion without restarting traversal. Resume storage is capped independently of point count at 16,384 × 384 bytes. Ring copy and spacing partials each dispatch all live rows in one submission; they perform bounded O(N) trivial work. Spacing partial sums use 4,096-row groups with 64 lanes (64 additions per lane plus six reduction levels); the final reduction combines at most 256 partial sums. There is no point-count-sized single-thread reduction. Internal test knobs reduce the traversal limits without changing the serialized config. These are work bounds, not a measured wall-clock guarantee. 1M-point timing not measured.

Each page uses `SpatialIndexCache::QueueGpuCompute` with immediate latency and a 32-byte diagnostic readback. Remaining CPU traffic is explicit: row-map uploads on a cold index, output-base uploads if existing CPU fields are not resident, and 4/32-byte control resets. Accept reads each final field once. GPU input uploads count the canonical position property; repeated input revisions reuse it. Workspaces and immutable per-submission property leases survive cancellation until the queue participant is idle/completed.

Score and uint32 mask rings are reserved together at admission. Full Vulkan uses float score and uint32 mask device storage; Accept converts live results into the configured scalar storage through the existing checked publisher. Canonical preview rings are discarded after conversion when their types differ from the CPU fields; they are never bound as a different storage type. The score becomes observable only after suppression and the completed ring copy. Keypoints has no iterative output: this terminal point is its natural preview interval, so no `gpu_preview_interval` is needed. Existing native float/uint32 deleted-row output bytes are copied before live results; new deleted rows remain zero. For alternate CPU storage, canonical device previews zero deleted rows and Accept preserves their original typed CPU bytes. Accept reads both fronts and uses the existing atomic keypoint publisher in one history entry through `PointScalarTransaction`'s optional companion field. Input, deletion and either output revision can refuse Accept with a reason. Discard and panel detach release both rings; Stop before the natural preview discards the incomplete calculation. Batch and agent execution auto-accept. Refused stale Accept terminates stale; publication and submission errors retain their own status. Duplicate active requests terminate busy instead of waiting for an unregistered callback.

| `method.engine-integration` field | Publication |
| --- | --- |
| Keypoints | GPU preview: yes (score); commit via PointScalarTransaction (score plus companion mask, one undoable keypoint publication). |

Measured delta (RTX 3050 under Xephyr, 2026-10-01): all 11 keypoint cases in
`IntrinsicPointLBVHGpuTests` pass with score Linf 0 and exact masks, including the resident
preview/discard/repeat/paged case, 4,096 identical points, subnormal scores, deleted slots and
Int32/Double storage. A timing measurement at 1M points is still outstanding.

Vulkan parity tests use absolute score tolerance 1e-5 for the unit-scale fixtures: CPU source-order and GPU LBVH-order accumulation/eigensolving can round differently before float publication. Spacing also may differ at float rounding boundaries: GPU tree-order nearest-neighbor evaluation and parallel double summation differ from the CPU reference order; resolved spacing and automatic radii use fixture-scale tolerances rather than bit equality. Masks are compared exactly, with equal published scores resolved by lowest original source index on both backends. Near-but-unequal score ordering is not relaxed by that tolerance. The readiness-gated tests include observed-front readback, Discard restoration, unchanged-input zero upload, forced multi-page traversal and 4,096 identical points (invalid zero spacing must fail without device loss).

## Runtime and config

The editor/config command surface is owned by
`Extrinsic.Runtime.PointAnalysisOperations`. It uses the shared
`EditorProcessingCommands` handle and explicit completion callbacks for newly
queued jobs. `PrepareEditorPointAnalysisFrame` supplies guarded commands,
completion sinks and copied results to the editor. See
[processing compilation locality](sandbox-editor-feature-boundaries.md#processing-compilation-locality).

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

Unknown domain resolves to the primary point domain. Outputs must be distinct, count-matched typed properties on the input domain and cannot overwrite position, topology or deletion data. Results report requested/actual backend, live/total/written/keypoint counts, resolved scale, index reuse, GPU batches, largest indexed support and CPU/GPU elapsed times. Full Vulkan reports completion-gated submissions; hybrid Vulkan reports neighborhood batches. Largest indexed support is zero on the streaming reference path; it is not a count of selected keypoints. CPU timing includes scale preparation for hybrid Vulkan requests. Full Vulkan leaves the CPU/neighborhood timer fields at zero; its benchmark measures request-through-publication latency, including frame and readback waits, rather than claiming kernel-only timing. New CPU saliency and mask become available together after Accept; the completed score preview is available before Accept. Show buttons select the observed property.

## Verification entry points

`Test.KeypointAnalysis.cpp` covers analytic scores, equal-score suppression, malformed supplied rows, independent exhaustive support and nearest-live spacing. `Test.KeypointAnalysisOperations.cpp` covers all canonical domains, shared config, history, visualization and stale/cancelled jobs. `PointLBVHGpuSmoke.Keypoint*` cases compare masks/scores at explicit and automatic radii, then separately exercise stale input, cancellation with reaping, radius overflow and partial-chain submission rejection.

`geometry.point_lbvh.keypoint_runtime_smoke` measures eight-domain requests through publication, with one cold warmup and one measured request set. `INTRINSIC_KEYPOINT_BENCHMARK_OUTPUT` writes raw diagnostics. Debug smoke times do not establish a performance improvement. [RUNTIME-225](../../tasks/done/RUNTIME-225-iss-keypoint-spatial-backends.md) owns verification and terminal review.

`geometry.point_lbvh.keypoint_compute_runtime_smoke` uses the same seeded dataset,
parameters and latency scope for the full Vulkan backend. The corresponding
`KeypointVulkanCompute*` cases additionally exercise all-domain publication,
planar/zero scores, the reference isotropic floor, and stale/cancelled GPU work.
[RUNTIME-247](../../tasks/done/RUNTIME-247-keypoint-vulkan-compute.md) records
verification and measured numerical limitations for this backend.

# Point spacing and splat radii

**View → Point Spacing and Radii** (also in mesh, graph and point-cloud
Processing menus) estimates a named float radius on any canonical float3 sample
domain. Choose the entity, positions, output, neighbor backend, k and scale.
**Estimate radii** uses validated config and publishes an undoable property;
**Show radii** colors samples through the shared scalar visualization recipe.
The result also reports nearest-other spacing, bounds and centroid.

## Formulation and units

For n samples, request `min(n,max(k,1)+1)` nearest candidates, sorted by squared
distance then source ID. Remove self by ID, retaining coincident peers. The
radius is `scale * mean_j(length(p_j-p_i))` over the retained candidates. A
coincident tie that omits self can leave k+1 peers. Spacing is the minimum
nonself distance, aggregated separately from the mean-k radius. Zero scale and
zero spacing/radius are valid. At least two live samples are needed for radii.

Distances and radii use the input property's coordinate units, independently of
entity transforms. This preserves the existing isotropic radius heuristic; it
does not guarantee overlap or hole-free reconstruction. [Surface Splatting
(Zwicker et al., 2001)](https://vcg.seas.harvard.edu/publications/20010101-surface-splatting)
and [High-Quality Surface Splatting (Botsch et al., 2005)](https://graphics.rwth-aachen.de/media/papers/splatting1.pdf)
define filtered footprints and projection. Their EWA/elliptical rendering
formulations are separate from this mean-distance estimator.

The renderer currently expects pixel sizes, and point-cloud geometry residency
rejects named size sources. **Show radii** displays scalar colors; it does not
change splat sizes. [RUNTIME-222](../../tasks/backlog/runtime/RUNTIME-222-model-space-point-radius-rendering.md)
owns model-space radius binding, upload and camera projection.

## Geometry and runtime contracts

`EstimateRadii` accepts a float3 span; the Cloud overload delegates using stored
rows. `EstimateRadiiFromNeighbors` accepts row-major candidate IDs and performs
CPU reduction. The result includes full nearest-other spacing in `Statistics`.
`ComputeStatistics` also accepts a span, retaining `SpacingSampleCount` and the
historical sample stride `floor(n/sampleCount)`. Its supplied-neighbor overload
expects two candidate IDs per sampled row; singleton statistics require none.
Bounds and centroid always cover the full input.

Nonfinite inputs, negative/nonfinite scale, malformed candidate rows or
unrepresentable float results fail with `nullopt`. Distances, squared distances,
means, scale products, centroid and statistics accumulate in double with
contraction disabled; coordinates are converted before subtraction. A separation
of 1e-20 with scale 1e20 therefore produces a radius near one even on float-FTZ
devices. The supplied-row caller guarantees
nearest membership; the kernel checks cardinality, bounds, uniqueness and
order. Very large k is clamped without overflowing k+1. CPU octree streams a
single neighborhood, retaining O(n+k) query storage.

Runtime excludes deleted rows (paired edge deletion for halfedges), maps live
compact indices back to source slots, and preserves deleted output values,
unrelated properties and topology. Position, deletion and output revisions
guard queued publication and undo/redo. Inputs from all eight domains use the
same preflight for execution, config and UI discovery.

- `cpu_octree`: default independent CPU reference.
- `cpu_lbvh`: shared immutable `SpatialIndexCache` snapshot.
- `vulkan_lbvh`: resident LBVH kNN, radius values and fixed-order double statistics
  in `Graphics.PointScalarAnalysis`; no neighborhood download or CPU reduction.

GPU requests permit k=0..63 (zero is floored to one), at most 2^20 live samples,
2^24 property rows and 2^24 neighbor entries. Both LBVH paths require coordinates
within 1e18; CPU LBVH supports 2^24 samples. Vulkan requires float64 support,
framed cache/jobs, float output storage and normal-or-zero float inputs/parameters.
Other scalar storage is explicitly refused; use CPU for its checked conversions.
The persisted query batch size does not paginate the resident submission.
Index ownership remains with the existing cache.

## Config and diagnostics

Section `sandbox.point_spacing`, schema `intrinsic.runtime.sandbox.point_spacing`,
version 1:

```json
{
  "entity": 0,
  "backend": "cpu_octree",
  "positions": {"domain":"unknown", "name":"v:position", "kind":"vec3"},
  "radii": {"domain":"unknown", "name":"radii", "kind":"float"},
  "k_neighbors": 6,
  "scale_factor": 1,
  "gpu_query_batch_size": 4096
}
```

Unknown domain resolves to the entity's primary point domain. Input/output
references must be distinct and on the same domain. Existing output types and
cardinality must match; topology/deletion names are protected.
`ApplyEditorPointSpacingConfig` and `ApplyEditorConfiguredPointSpacing` are the
shared UI/config/agent path. `PreviewEditorPointSpacingCommand` and
`GetEditorPointSpacingInputCatalog` share execution preflight. Results include
requested/actual backend, live/slot/written counts, radius and spacing ranges,
cache reuse, GPU batch count and CPU/GPU elapsed times.

## Verification entry points

`Test.PointSpacing.cpp` supplies analytical spacing/radius oracles, exhaustive
candidate comparisons, sampled statistics, ties, malformed input and extreme k.
`Test.PointSpacingOperations.cpp` exercises all domains, config, cache reuse,
history, scalar visualization and stale/cancelled publication. The opt-in
`PointLBVHGpuSmoke.PointSpacingPublishesAcrossDomainsAndPreservesCandidatePolicy`
compares actual Vulkan publication to CPU octree for cold/warm queries, scale 2
at k=63, dense coincident peers, deletion, staleness, cancellation and history.

`geometry.point_lbvh.spacing_runtime_smoke` records the bounded eight-domain
fixture. Set `INTRINSIC_SPACING_BENCHMARK_OUTPUT` to a raw JSON path, then seal
and validate with the repository benchmark tools. Timings include frames,
readback and publication; summed request timings overlap. The smoke provides
no scaling or performance improvement claim. [RUNTIME-221](../../tasks/done/RUNTIME-221-point-spacing-spatial-backends.md)
tracks the slice; the [consumer inventory](spatial-index-consumers.md) tracks
remaining spatial integrations.

The [2026-09-10 verification record](../../ara/evidence/tables/spacing_vulkan_verification_2026-09-10.md)
binds the bounded CPU/Vulkan result to C83. The recorded maximum radius/spacing
error is zero at a 1e-5 tolerance, with CPU distance reduction retained.

## Resident scalar transaction

| method.engine-integration | Publication |
| --- | --- |
| Point spacing | GPU preview: yes; commit via `GpuFrontReadback` → `PublishPointScalarField` → `BindRevision(key, revision, publication)`. |

`Runtime.PointScalarTransaction` shares the density/weight lifecycle: colormap
observation, explicit Accept/Discard, stale watches, detach discard, undoable
publication and automatic batch/agent Accept. Deleted output rows retain their
resident base values. Result/agent output includes upload bytes, residency hits
and Accept readback bytes; no CPU neighborhood stage remains.

`PointScalarTransaction` mock tests and
`RUNTIME298PointScalarResidency.ParityResidentSecondRunAndDiscard` cover the port.
The smoke records measured maximum absolute delta, with 2e-5 allowed for device
final float publication on its O(1) fixture; existing multi-domain
spacing/statistics comparisons retain 1e-5. It checks deleted rows, zero input
upload on a second run and Discard retaining CPU rows. GPU execution is pending;
the historical CPU-reduction verification does not validate this device kernel.

CPU octree and CPU LBVH scalar queries use double keys and box bounds; GPU scalar
queries use the same double squared-distance expression and index tie-break.
Candidate caps apply before self removal. Distances are bounded away from double
underflow by float coordinate storage; Vulkan continues refusing subnormal input
coordinates because building the shared LBVH uses float arithmetic. Boundary
smokes additionally use relative checks for tiny published statistics.

Supplied-neighbor APIs validate monotonic squared distances in double, promoting
coordinates before subtraction and breaking exact ties by source ID. Candidate
rows sorted using float-rounded distance keys can therefore be rejected even
when their membership is correct. This applies to KDE, radii/statistics and
local-distance-ratio `*FromNeighbors` entry points; bilateral filtering retains
its float-distance contract.

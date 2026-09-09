# Local Gaussian kernel density

**View → Kernel Density**, also exposed in mesh, graph and point-cloud Processing
menus, estimates a named float density property on the selected position domain.
Choose a canonical float3 input (including face centers or edge/halfedge samples),
neighbor backend, k and bandwidth. **Estimate density** applies the shared config
and command; **Show density** uses the scalar visualization recipe. Undo restores
only the output property. Deleted rows and unrelated properties remain intact.

## Formulation

The existing `Geometry.PointCloud.Utils` estimator is retained. For n live points,
query `min(n,max(k,2)+1)` nearest candidates, ordered by squared distance and
source ID, then remove the source ID. Coincident peers remain eligible; a row
whose source falls outside a coincident tie can retain k+1 peers. Average the
normalized isotropic Gaussian over the retained candidates:

`density_i = mean_j [(2π)^(-3/2) h^(-3) exp(-||p_i-p_j||²/(2h²))]`.

The result has units of inverse cubed input length. Bandwidth is in input length
units. A positive configured bandwidth is global and fixed; zero selects the
inherited heuristic `max(1e-8, 1.06 max(stddev(d),mean(d)) n^(-1/5))`, where d is
nearest-other spacing and standard deviation uses population variance.

This is a local Gaussian score with spacing-based bandwidth. It does not sum
all n Gaussian contributions, estimate a covariance bandwidth matrix or claim a
normalized probability distribution over the ambient domain. It preserves the
existing float arithmetic; invalid/nonfinite input, malformed supplied candidate
rows, or unrepresentable float kernel values reject the operation before output
publication. Underflowed individual distant contributions can be zero.

Literature reviewed: [Silverman (1986), §§2.4–2.5](https://ned.ipac.caltech.edu/level5/March02/Silverman/Silver_contents.html)
provides the kernel/nearest-neighbor background. The engine's spacing floors and
local averaging are explicit formulation choices. [SciPy's multivariate bandwidth
rules](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.gaussian_kde.html)
and [Gramacki & Gramacki (2016)](https://arxiv.org/abs/1511.07482) describe different
bandwidth/FFT approaches; this integration does not adopt those estimators.

## Backends and ownership

- `cpu_octree` remains the default CPU reference.
- `cpu_lbvh` borrows an immutable canonical-domain `SpatialIndexCache` snapshot.
- `vulkan_lbvh` uses framed kNN queries and readback from that cache, followed by
  CPU bandwidth selection and Gaussian evaluation. It requires the operational
  GPU query path and JobService; unsupported requests fail explicitly.

The same candidate rows supply nearest-other spacing and Gaussian support, so
no separate bandwidth query or radius cutoff is needed. Vulkan accepts k=0..63
(k<2 is floored to two), at most 2^20 live samples and query chunks of 1..16384.
CPU LBVH accepts at most 2^24 live samples. Both LBVH paths require coordinates
within 1e18. Supplied-candidate LBVH storage costs O(n min(n,max(k,2)+1)); the CPU reference
streams rows with O(n+k) query scratch. GPU transient buffers
are additionally bounded by chunk size. CPU octree has no Vulkan k limit.

Distances use the bound property's coordinates. Entity transforms do not alter
this metric; bind world-space samples when that is the intended measurement.
Runtime maps compact live IDs back to original slots, checks source/deletion and
output revisions before publication, and rejects stale or cancelled results.
The owning source domain is preserved across all eight canonical mesh, graph and
point-cloud domains. This adds no ECS index component or new runtime service.

## Config and command path

Section `sandbox.kernel_density`, schema `intrinsic.runtime.sandbox.kernel_density`,
version 1, round-trips these controls:

```json
{
  "entity": 0,
  "backend": "cpu_octree",
  "positions": {"domain":"unknown", "name":"v:position", "kind":"vec3"},
  "density": {"domain":"unknown", "name":"density", "kind":"float"},
  "k_neighbors": 15,
  "bandwidth": 0,
  "gpu_query_batch_size": 4096
}
```

Unknown domain resolves to the entity's primary point domain; explicit domains
must match. Inputs and outputs have distinct names; existing output types and
cardinality must match. Topology/deletion properties are protected. Config/UI
use `ApplyEditorKernelDensityConfig` then `ApplyEditorConfiguredKernelDensity`;
agents can use the same path. `PreviewEditorKernelDensityCommand` and
`GetEditorKernelDensityInputCatalog` share source preflight with execution.
Results identify the requested/actual backend, used bandwidth, density range,
written/live/total counts, cache reuse, batch count and CPU/GPU elapsed times.

## Verification scope

`Test.KernelDensity.cpp` checks analytical Gaussians, bandwidth, exhaustive nearest
candidates, legacy Cloud/span agreement, ties and invalid input.
`Test.KernelDensityOperations.cpp` covers source domains, config, history,
visualization and queued stale/cancelled work. The opt-in
`PointLBVHGpuSmoke.KernelDensityPublishesAcrossDomainsAndPreservesCandidatePolicy`
compares actual readback-driven publication against CPU octree for cold/warm
automatic bandwidth and explicit bandwidth at k=63, plus dense coincident peers.

`geometry.point_lbvh.density_runtime_smoke` records the bounded eight-domain
fixture. Set `INTRINSIC_DENSITY_BENCHMARK_OUTPUT` to a raw JSON path while running
the case, then seal and validate with the repository benchmark tools. Timings
include frames/readback/publication; summed request timings overlap. This smoke
makes no scaling, performance improvement or default-selection claim.

The [consumer inventory](spatial-index-consumers.md) tracks remaining candidates;
[RUNTIME-220](../../tasks/active/RUNTIME-220-kernel-density-spatial-backends.md)
records this slice.

The [2026-09-09 verification record](../../ara/evidence/tables/density_vulkan_verification_2026-09-09.md)
binds the bounded CPU/Vulkan result to C82. It reports zero observed GPU density
delta at 1e-5 tolerance, with CPU bandwidth and evaluation retained.

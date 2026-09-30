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
normalized probability distribution over the ambient domain. Coordinates are
promoted before subtraction; distances, squared distances, bandwidth estimation,
Gaussian values, normalization and ordered reductions use double with no FMA
contraction. Float properties round only at publication. Invalid inputs or
unrepresentable outputs refuse publication. Exponents below -708 yield a zero kernel on CPU and Vulkan. Normalized
products and per-point means below `DBL_MIN` likewise flush to zero before the
operation; these neighbors still count in the averaging denominator. Widely
separated points therefore produce finite densities without refusing the run. Float-subnormal
kernels such as exp(-90) remain supported and may normalize to normal outputs.

Literature reviewed: [Silverman (1986), §§2.4–2.5](https://ned.ipac.caltech.edu/level5/March02/Silverman/Silver_contents.html)
provides the kernel/nearest-neighbor background. The engine's spacing floors and
local averaging are explicit formulation choices. [SciPy's multivariate bandwidth
rules](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.gaussian_kde.html)
and [Gramacki & Gramacki (2016)](https://arxiv.org/abs/1511.07482) describe different
bandwidth/FFT approaches; this integration does not adopt those estimators.

## Backends and ownership

- `cpu_octree` remains the default CPU reference.
- `cpu_lbvh` borrows an immutable canonical-domain `SpatialIndexCache` snapshot.
- `vulkan_lbvh` records `Graphics.PointScalarAnalysis` over the canonical resident
  position slot and cached LBVH. kNN rows, nearest spacing, automatic bandwidth,
  Gaussian evaluation and fixed-order double reductions stay on the device.
  The host supplies double `pow(n,-0.2)` and `(2*pi)^1.5` constants; the device
  retains double scratch bandwidth and nearest distances between passes.

The same candidate rows supply nearest-other spacing and Gaussian support.
Vulkan accepts k=0..63 (k<2 is floored to two), at most 2^20 live samples,
2^24 property rows and 2^24 neighbor entries. CPU LBVH accepts 2^24 samples.
Both LBVH paths require coordinates within 1e18. Vulkan refuses subnormal
coordinates/float parameters and non-float output storage. It requires shader
float64 (the shared workspace capability contract), a framed cache and jobs.
The persisted query batch size remains config-compatible but does not paginate
this single resident compute submission. CPU octree has no Vulkan k limit.

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
[RUNTIME-220](../../tasks/done/RUNTIME-220-kernel-density-spatial-backends.md)
records this slice.

The [2026-09-09 verification record](../../ara/evidence/tables/density_vulkan_verification_2026-09-09.md)
binds the bounded CPU/Vulkan result to C82. It reports zero observed GPU density
delta at 1e-5 tolerance, with CPU bandwidth and evaluation retained.

## Resident scalar transaction

| method.engine-integration | Publication |
| --- | --- |
| Kernel density | GPU preview: yes; commit via `GpuFrontReadback` → `PublishPointScalarField` → `BindRevision(key, revision, publication)`. |

The panel offers Accept/Discard and stale refusal reasons; detach discards.
Batch/agent Apply accepts automatically. The scalar front is observed by the
colormap before Accept, with CPU rows unchanged. Deleted output rows are copied
from the resident base, or zero for a new output. Results and agent messages
report input upload bytes, residency hits and Accept readback bytes. There is no
CPU neighborhood reduction. A second unchanged-input run reuses resident input.

`PointScalarTransaction` contract tests cover publication, revision binding,
undo, discard, stale inputs/outputs and config round trips. The opt-in
`RUNTIME298PointScalarResidency.ParityResidentSecondRunAndDiscard` compares the
resident result to the CPU reference, records each measured maximum absolute
delta, and checks zero-upload reuse and deleted-row preservation. Its fixture
uses a 2e-5 absolute tolerance plus relative checks for final float publication; existing
multi-domain tests retain their 1e-5 bounds. GPU execution of this port is pending;
previous neighborhood-only parity records do not establish parity for this kernel.

### Numerical query and exponential contract

The CPU octree/LBVH queries opt into double box distances and double candidate
keys, ordered by `(squared distance, source index)`. Resident shaders use
`point_lbvh_double.glsl` over the existing nodes, including identical candidate
caps and self removal. The default standalone float query ABI is unchanged.
No float-distance pruning or limited slack-candidate reranking is used here.

`exp_double.glsl` evaluates a degree-13 Taylor polynomial after split-ln(2)
range reduction. On [-708,0], its truncation error is below 5e-18 relative;
allowing rounded, uncontracted binary64 operations gives a conservative 8e-15
relative error budget. A host translation of the exact shader expression,
compiled with contraction off, measured 2.219e-16 maximum relative difference
from `std::exp` over 4,003,064 uniformly spaced and reduction-boundary samples.
This is a host arithmetic check; real-device parity is exercised by
`RUNTIME298PointScalarResidency.ParityResidentSecondRunAndDiscard`, including
h=1e-12 with points 1.34e-11 apart and relative error checks that reject zero.

Supplied-neighbor APIs validate monotonic squared distances in double, promoting
coordinates before subtraction and breaking exact ties by source ID. Candidate
rows sorted using float-rounded distance keys can therefore be rejected even
when their membership is correct. This applies to KDE, radii/statistics and
local-distance-ratio `*FromNeighbors` entry points; bilateral filtering retains
its float-distance contract.

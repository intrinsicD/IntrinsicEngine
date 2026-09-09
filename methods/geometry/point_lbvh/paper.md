# Point LBVH formulation

Tero Karras, [Maximizing Parallelism in the Construction of BVHs, Octrees,
and k-d Trees](https://research.nvidia.com/publication/2012-06_maximizing-parallelism-construction-bvhs-octrees-and-k-d-trees),
HPG 2012, DOI 10.2312/EGGH/HPG12/033-037, defines the binary radix-tree
construction used here. We use 30-bit Morton codes and append original
32-bit point indices to disambiguate duplicate codes. Internal nodes follow
the paper's independent range/split construction; leaves retain source slots.

The [2013 Karras/Aila extension](https://research.nvidia.com/publication/2013-07_fast-parallel-construction-high-quality-bounding-volume-hierarchies)
optimizes tree quality for ray traversal. That optimization is outside this
point-neighbor implementation. Framework24's `bcg_bvh.cuh` and k-means CUDA
consumer informed the two ownership cases: reusable entity data and changing
operation-local centroid data. The engine implementation is native C++/GLSL.

Queries minimize squared Euclidean distance in the input property's coordinate
space. Equal distances choose the smallest original index. Radius queries use
an inclusive boundary, return the lowest original indices in ascending order,
and report the total hit count even when the caller's capacity is smaller.
No topology is changed and no world-space metric is inferred from transforms.

The correctness baseline is an independent exhaustive CPU scan. The CPU LBVH
is checked against it before Vulkan implementation. Float coordinates must be
finite and at most 1e18 in magnitude, keeping distance arithmetic finite.
Morton quantization affects hierarchy shape, not the exact leaf tests.

K-nearest queries return up to k eligible samples sorted by `(squared distance,
original source ID)`. Exclusion removes one source identity, never all points
at the same position. CPU k=0 returns empty; k larger than the eligible set
returns the whole set. Vulkan supports k=1..64 with matching output capacity;
unsupported requests are rejected. The exhaustive oracle sorts every eligible
point. CPU traversal maintains a bounded max-heap; Vulkan inserts into the
invocation's sorted result slice. Both prune only boxes strictly beyond the
current kth distance, preserving deterministic ties.

The supplied-index PCA adapter follows [Hoppe et al. 1992](https://www.hhoppe.com/proj/recon/)
local tangent-plane estimation and the existing orientation implementation.
[Mitra, Nguyen and Guibas' neighborhood analysis](https://graphics.stanford.edu/~niloy/research/normal_est/normal_estimation_socg_03_ijcga_04.html)
highlights the effects of noise, curvature and sampling density; adaptive or
robust neighborhood changes are outside this adapter. It deliberately retains
the existing k+1 query followed by source-identity filtering. With many tied
coincident samples, the query's ID need not occur in those k+1 hits, so replacing
this with k hits excluding self would change current PCA support. Radius mode
collects every CPU hit before the existing filter/order/PCA logic. The supplied
index must exactly match the input coordinates and order. MST orientation and
fallback rules remain owned by the estimator.

`geometry.point_lbvh.knn_smoke` measures a 512-point paraboloid, with one warmup
and four measured repetitions: CPU build, exhaustive and warm LBVH queries,
and complete KD-tree versus supplied-LBVH PCA execution (normal k=15). Supplied PCA time
excludes the caller-owned LBVH build (reported separately); KD-tree PCA time
includes its internal build. This fixture is correctness and timing plumbing,
not evidence of a general speed improvement or a new default.

The Vulkan normal consumer obtains the same candidate neighborhoods in framed
batches, preserving total radius counts and rejecting support above 1024 hits.
The CPU estimator accepts validated CSR candidate rows and owns all filtering,
PCA, fallback and MST orientation. kNN retains the reference's extra candidate,
so normally k/minimum must be at most 63 for the 64-candidate GPU limit.

`geometry.point_lbvh.normal_runtime_smoke` exercises eight canonical property
domains with 66 slots each, deleted rows and coincident samples. It records a
cold kNN run, one warm measured run, a radius run and separate CPU references.
The timed method scope includes submission through publication; CPU reference
timing also includes output capture/reset. GPU neighborhood timing includes
frame waits/readback and is not kernel-only timing. This bounded smoke is not
a scaling benchmark and does not qualify a performance-based default change.

Run the opt-in runtime smoke on a Vulkan/display-capable host after building
`IntrinsicPointLBVHGpuTests` with `ci-vulkan`:

```bash
mkdir -p /tmp/normal-runtime-smoke/raw
INTRINSIC_NORMAL_BENCHMARK_OUTPUT=/tmp/normal-runtime-smoke/raw/result.json \
  ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^PointLBVHGpuSmoke.NormalNeighborhoods' -L gpu -L vulkan --timeout 120
python3 tools/benchmark/seal_benchmark_results.py \
  --root /tmp/normal-runtime-smoke/raw --output-root /tmp/normal-runtime-smoke/canonical
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/normal-runtime-smoke/canonical
```

Use a fresh output directory for each retained run. Raw timing sums across
eight simultaneous requests overlap; compare complete wall times rather than
adding their elapsed neighborhood timers. Dirty-worktree smoke results remain
non-claim-eligible.

## Statistical and radius outlier consumer

The [outlier method contract](../../../docs/architecture/outlier-analysis.md)
records the SOR/ROR literature intake and existing population-variance choice.
Runtime kNN excludes the source row; radius classification consumes complete
total counts with capacity one. Classification and publication remain CPU work.
The outlier runtime benchmark covers cold/warm kNN and radius publication on
eight canonical domains, with independent CPU octree comparison.

## Local Gaussian density consumer

The [density contract](../../../docs/architecture/kernel-density.md) fixes the
local Gaussian estimator and inherited spacing heuristic. One kNN candidate
batch supplies both nearest-other spacing and density support; source filtering
remains after k+1 selection. Vulkan uses k<=63 and CPU bandwidth/evaluation.
`geometry.point_lbvh.density_runtime_smoke` times eight-domain cold/warm requests
and explicit bandwidth at k=63 against CPU octree. Set
`INTRINSIC_DENSITY_BENCHMARK_OUTPUT` while running the named density GPU smoke;
seal and validate its output as for the normal runtime smoke above.

## Point spacing and radius consumer

The [spacing/radius contract](../../../docs/architecture/point-spacing.md) preserves min(n,max(k,1)+1) candidates followed by self removal. Radii average retained distances times scale; nearest-other spacing is a separate statistic from the same rows. CPU octree remains reference; cached CPU/Vulkan LBVH provide candidates. The geometry supplied-statistics adapter retains sampled-row stride. `geometry.point_lbvh.spacing_runtime_smoke` measures framed eight-domain publication and CPU comparisons; it does not evaluate splat coverage or establish a performance win.

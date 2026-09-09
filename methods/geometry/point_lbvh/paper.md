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

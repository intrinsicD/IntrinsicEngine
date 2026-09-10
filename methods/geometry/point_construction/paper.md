# Selected construction formulations

[Hoppe et al., SIGGRAPH 1992](https://hhoppe.com/recon.pdf)
estimates neighborhood tangent planes, orients them through a proximity graph,
and evaluates a signed-distance field for contour extraction. Its plane anchors
are neighborhood centroids, and its domain test can leave field values undefined.
The engine retains its existing, distinct sample-anchored formulation:

`d(q) = dot(q - p_nearest, n_nearest)`.

With `k > 1`, the engine evaluates the closest `min(k+1, N)` samples, using
Gaussian distance weights and agreement with the closest sample's normal. The
bandwidth is the farthest retained squared distance times the squared sigma
scale, clamped by the existing `1e-8` kernel floor. Normal agreement is clamped
nonnegative before exponentiation. The existing fallback uses the closest
sample's plane when the weight sum is too small. Supplied normals are normalized;
the optional initializer remains the existing CPU PCA/MST implementation.
The field is sampled on the existing padded regular grid and extracted with
the engine's Marching Cubes implementation.

The kNN graph construction is an independent geometric utility. Each source
sample requests `min(k+1, N)` candidates. It skips itself and distances at or
below the epsilon threshold, accepting at most `k` neighbors from that fixed
row. It does not request replacement candidates for duplicates. Union mode
inserts an undirected edge if either directed candidate list contains the pair;
mutual mode requires both directions. Canonical edge deduplication remains in
`BuildKNNGraphFromIndices`.

The independent exhaustive CPU query oracle orders by float squared Euclidean
distance and source index. CPU LBVH and Vulkan use the same intended query
contract; GPU rows are sorted again using CPU distances before reduction.
This sorting cannot recover a different candidate set selected by rounding at
a near tie. The legacy octree API remains available, including its distinct
nearest-tie behavior. Legacy comparisons use non-tied fixtures, while new
backend comparisons explicitly test source-ID ties and duplicate samples.

[Poisson surface reconstruction](https://hhoppe.com/proj/poissonrecon/) and its
screened extension solve a different global field problem. They are not
substituted for this local tangent-plane field, and a proximity LBVH does not
replace their linear solve. Likewise, graph shortest paths and existing mesh
adjacency are not reconstructed from proximity edges.

The runtime measures distance in the chosen property's coordinate system.
Generated geometry is baked into the source's captured world transform,
including reversed triangle winding under reflection, then receives standard
mesh/graph materialization. Inputs are immutable job snapshots. Completion
checks property/deletion revisions and the source hierarchy transform before
publishing a separate entity through command history.

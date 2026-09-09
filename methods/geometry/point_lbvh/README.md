# Point LBVH

`Geometry.PointLBVH` provides exhaustive CPU query oracles and a deterministic
Morton-sorted point tree. `Extrinsic.Graphics.PointLBVH` owns reusable Vulkan
storage and records GPU bounds, Morton codes, bitonic sort, radix-tree
construction, and nearest/k-nearest/radius traversal with source-ID exclusion.

The PCA normal estimator accepts a supplied CPU LBVH or complete neighborhood
spans. The normal workflow can obtain those neighborhoods from framed Vulkan
queries; PCA/orientation remain on the CPU. Its KD-tree default and neighborhood
policy remain unchanged.

Entity consumers use `Extrinsic.Runtime.SpatialIndexCache`; Vulkan k-means uses
the same kernels with a private centroid workspace. See the
[ownership and API contract](../../../docs/architecture/spatial-indices.md),
[formulation](paper.md), and [manifest](method.yaml).

[Outlier analysis](../../../docs/architecture/outlier-analysis.md) uses cached CPU
or framed Vulkan neighbors for statistical/radius detection with named masks and
scores; classification remains on CPU.

[Local Gaussian density](../../../docs/architecture/kernel-density.md) uses shared
CPU/Vulkan nearest candidates with CPU bandwidth/evaluation and named publication.

[Point spacing and radii](../../../docs/architecture/point-spacing.md) use shared kNN candidates, CPU distance reductions, and canonical-domain radius publication.

# Point LBVH

`Geometry.PointLBVH` provides exhaustive CPU query oracles and a deterministic
Morton-sorted point tree. `Extrinsic.Graphics.PointLBVH` owns reusable Vulkan
storage and records GPU bounds, Morton codes, bitonic sort, radix-tree
construction, and nearest/k-nearest/radius traversal with source-ID exclusion.

The existing PCA normal estimator accepts a supplied CPU LBVH; its KD-tree
default and neighborhood policy remain unchanged.

Entity consumers use `Extrinsic.Runtime.SpatialIndexCache`; Vulkan k-means uses
the same kernels with a private centroid workspace. See the
[ownership and API contract](../../../docs/architecture/spatial-indices.md),
[formulation](paper.md), and [manifest](method.yaml).

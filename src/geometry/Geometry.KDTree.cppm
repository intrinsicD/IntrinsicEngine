// Compatibility names for the point/AABB k-nearest and radius index. The median-split
// tree and its queries live in Geometry.BVH; despite the name this is not a
// space-partitioning kd-tree.
module;

export module Geometry.KDTree;

export import Geometry.BVH;
import Geometry.SpatialQueries;

export namespace Geometry
{
    using KDTree = BVH;
    using KDTreeBuildParams = BVHBuildParams;
    using KDTreeBuildResult = BVHBuildResult;
    using KDTreeKNNResult = BVHKNNResult;
    using KDTreeRadiusResult = BVHRadiusResult;
}

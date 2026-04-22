module;

export module Extrinsic.ECS.Component.Culling.Proxy;

import Geometry.Octree;
import Geometry.ConvexHull;

export namespace Extrinsic::ECS::Components::Culling
{
    struct CullableTag{};

    struct Proxy
    {
        Geometry::ConvexHull LocalConvexHull;
        Geometry::Octree LocalPrimitiveOctree;
    };
}
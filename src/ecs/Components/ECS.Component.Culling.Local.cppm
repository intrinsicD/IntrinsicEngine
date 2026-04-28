module;

export module Extrinsic.ECS.Component.Culling.Local;

import Geometry.AABB;
import Geometry.Sphere;

export namespace Extrinsic::ECS::Components::Culling
{
    struct Bounds
    {
        Geometry::AABB LocalBoundingAABB{};
        Geometry::Sphere LocalBoundingSphere{};
    };
}
module;

export module Extrinsic.ECS.Component.Culling.World;

import Geometry.OBB;
import Geometry.Sphere;

export namespace Extrinsic::ECS::Components::Culling
{
    struct Bounds
    {
        Geometry::OBB WorldBoundingOBB{};
        Geometry::Sphere WorldBoundingSphere;
    };
}
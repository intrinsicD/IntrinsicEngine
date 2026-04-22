module;

#include <vector>

export module Extrinsic.ECS.Component.Collider;

import Geometry.Sphere;

export namespace Extrinsic::ECS::Components::Collider
{
    struct Component
    {
        std::vector<Geometry::Sphere> Spheres{};
    };
}

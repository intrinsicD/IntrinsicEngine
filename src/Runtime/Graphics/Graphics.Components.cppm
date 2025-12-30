module;
#include <memory>

export module Graphics:Components;

import :Geometry;
import :Material;
import Geometry;

export namespace ECS::MeshRenderer
{
    struct Component
    {
        Graphics::GeometryHandle Geometry;
        std::shared_ptr<Graphics::Material> MaterialRef;
    };
}

export namespace ECS::MeshCollider
{
    struct Component
    {
        std::shared_ptr<Graphics::GeometryCollisionData> CollisionRef;
        Geometry::OBB WorldOBB;
    };
}


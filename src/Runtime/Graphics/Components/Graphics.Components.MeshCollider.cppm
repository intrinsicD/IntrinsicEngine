module;
#include <memory>

export module Graphics.Components.MeshCollider;

import Graphics.Geometry;
import Geometry.OBB;

export namespace ECS::MeshCollider
{
    struct Component
    {
        std::shared_ptr<Graphics::GeometryCollisionData> CollisionRef{};
        Geometry::OBB WorldOBB{};
    };
}

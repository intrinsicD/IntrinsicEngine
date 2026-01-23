module;
#include <memory>
#include <cstdint>

export module Graphics:Components;

import :Geometry;
import :Material;
import Geometry;
import Core;

export namespace ECS::MeshRenderer
{
    struct Component
    {
        Geometry::GeometryHandle Geometry;
        Core::Assets::AssetHandle Material;

        // --- Render Cache ---
        // Allows RenderSystem to avoid AssetManager lookups once resolved.
        Graphics::MaterialHandle CachedMaterialHandle = {};
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

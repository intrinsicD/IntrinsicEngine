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
        Graphics::GeometryHandle Geometry;
        Core::Assets::AssetHandle Material;

        // --- Render Cache ---
        // Allows RenderSystem to avoid AssetManager lookups once resolved.
        Core::Assets::AssetHandle CachedMaterialHandle = {};
        uint32_t TextureID_Cache = ~0u; // ~0u indicates invalid/uninitialized
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

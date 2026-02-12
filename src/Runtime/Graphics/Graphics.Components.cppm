module;
#include <memory>
#include <cstdint>

export module Graphics:Components;

import :Geometry;
import :Material;
import Geometry;
import Core.Assets;

export namespace ECS::MeshRenderer
{
    struct Component
    {
        Geometry::GeometryHandle Geometry;
        Core::Assets::AssetHandle Material;

        // --- Retained Mode Slot ---
        static constexpr uint32_t kInvalidSlot = ~0u;
        uint32_t GpuSlot = kInvalidSlot;

        // --- Render Cache ---
        // Allows RenderSystem to avoid AssetManager lookups once resolved.
        Graphics::MaterialHandle CachedMaterialHandle = {};

        // Cached snapshot used by GPUSceneSync to detect when instance TextureID must be refreshed.
        Graphics::MaterialHandle CachedMaterialHandleForInstance = {};
        uint32_t CachedMaterialRevisionForInstance = 0u;
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

module;

#include <entt/fwd.hpp>

export module Graphics:Systems.GPUSceneSync;

import :GPUScene;
import :MaterialSystem;

export namespace Graphics::Systems::GPUSceneSync
{
    // Streams updated transforms into the retained GPUScene instance buffers.
    // Contract:
    //  - Consumes ECS::Components::Transform::WorldUpdatedTag emitted by TransformSystem.
    //  - Clears WorldUpdatedTag after processing.
    //  - Updates only entities that have an allocated MeshRenderer::GpuSlot.
    //
    // Extended contract:
    //  - Also refreshes instance TextureID when the referenced material changes (MaterialSystem revision bump).
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  const Core::Assets::AssetManager& assetManager,
                  const MaterialSystem& materialSystem,
                  uint32_t defaultTextureId);
}

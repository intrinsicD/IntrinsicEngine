module;

#include <entt/fwd.hpp>

export module Graphics:Systems.GPUSceneSync;

import :GPUScene;

export namespace Graphics::Systems::GPUSceneSync
{
    // Streams updated transforms into the retained GPUScene instance buffers.
    // Contract:
    //  - Consumes ECS::Components::Transform::WorldUpdatedTag emitted by TransformSystem.
    //  - Clears WorldUpdatedTag after processing.
    //  - Updates only entities that have an allocated MeshRenderer::GpuSlot.
    void OnUpdate(entt::registry& registry, GPUScene& gpuScene);
}

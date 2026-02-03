module;

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Graphics:Systems.GPUSceneSync.Impl;

import :Systems.GPUSceneSync;

import ECS;
import :Components;

namespace Graphics::Systems::GPUSceneSync
{
    void OnUpdate(entt::registry& registry, GPUScene& gpuScene)
    {
        // Stream only entities whose WorldMatrix changed this tick.
        // NOTE: For v1 we don't recompute bounds here; sphere bounds remain the initial local bounds.
        auto view = registry.view<
            ECS::Components::Transform::WorldMatrix,
            ECS::Components::Transform::WorldUpdatedTag,
            ECS::MeshRenderer::Component>();

        for (auto [entity, world, mr] : view.each())
        {
            if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
                continue;

            GpuInstanceData inst{};
            inst.Model = world.Matrix;

            // Keep the picking ID stable.
            // If selection/picking is missing, EntityID stays 0.
            if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                inst.EntityID = pick->Value;

            // Bounds remain the original local sphere (stored in GPUScene bounds buffer).
            // If you want correct scaled bounds, we should store local sphere per entity and update here.
            gpuScene.QueueUpdate(mr.GpuSlot, inst, /*sphereBounds*/ glm::vec4(0.0f));

            // Clear tag so we don't resend next tick.
            registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
        }
    }
}

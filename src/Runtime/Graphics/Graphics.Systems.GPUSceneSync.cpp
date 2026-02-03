module;

#include <entt/entity/registry.hpp>

module Graphics:Systems.GPUSceneSync.Impl;

import :Systems.GPUSceneSync;

import ECS;
import :Components;

namespace Graphics::Systems::GPUSceneSync
{
    void OnUpdate(entt::registry& registry, GPUScene& gpuScene)
    {
        // Contract:
        // - Streams only entities whose WorldMatrix changed this tick.
        // - Keeps picking ID stable if the Selection::PickID component exists.
        // - Bounds updates are currently disabled: the GPU keeps the originally uploaded local sphere.
        //   TODO(bounds): store per-entity local sphere (and track local-bounds dirty), then update a conservative
        //   world-space sphere here (handle non-uniform scale by expanding radius with max scale).
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

            gpuScene.QueueUpdate(mr.GpuSlot, inst, /*sphereBounds*/ {0.0f, 0.0f, 0.0f, 0.0f});

            // Clear tag so we don't resend next tick.
            registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
        }
    }
}

module;

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module ECS:HierarchyTraversal.Impl;

import :HierarchyTraversal;
import :Components.Transform;
import :Components.Hierarchy;

namespace ECS::Systems::HierarchyTraversal::Detail
{
    void UpdateNode(entt::registry& registry,
                    entt::entity entity,
                    const glm::mat4& parentMatrix,
                    bool parentDirty)
    {
        auto* local = registry.try_get<Components::Transform::Component>(entity);
        auto* world = registry.try_get<Components::Transform::WorldMatrix>(entity);
        auto* hierarchy = registry.try_get<Components::Hierarchy::Component>(entity);

        if (!local || !world)
        {
            return;
        }

        const bool localDirty = registry.all_of<Components::Transform::IsDirtyTag>(entity);
        const bool isDirty = localDirty || parentDirty;

        if (isDirty)
        {
            world->Matrix = parentMatrix * GetMatrix(*local);
            registry.emplace_or_replace<Components::Transform::WorldUpdatedTag>(entity);
            registry.remove<Components::Transform::IsDirtyTag>(entity);
        }

        if (!hierarchy || hierarchy->FirstChild == entt::null)
        {
            return;
        }

        entt::entity child = hierarchy->FirstChild;
        while (child != entt::null)
        {
            auto& childHierarchy = registry.get<Components::Hierarchy::Component>(child);
            UpdateNode(registry, child, world->Matrix, isDirty);
            child = childHierarchy.NextSibling;
        }
    }
}

namespace ECS::Systems::HierarchyTraversal
{
    void UpdateWorldTransforms(entt::registry& registry)
    {
        auto roots = registry.view<Components::Transform::Component, Components::Hierarchy::Component>();

        for (auto [entity, transform, hierarchy] : roots.each())
        {
            (void)transform;
            if (hierarchy.Parent == entt::null)
            {
                Detail::UpdateNode(registry, entity, glm::mat4(1.0f), false);
            }
        }
    }
}

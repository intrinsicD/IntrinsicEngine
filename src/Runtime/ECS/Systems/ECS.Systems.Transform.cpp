module;
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module ECS:Systems.Transform.Impl;
import :Systems.Transform;
import :Components.Transform;
import :Components.Hierarchy;

namespace ECS::Systems::Transform::Detail
{
    void UpdateHierarchy(entt::registry& reg, entt::entity entity,
                         const glm::mat4& parentMatrix, bool parentDirty)
    {
        // 1. Get Components
        auto* local = reg.try_get<Components::Transform::Component>(entity);
        auto* world = reg.try_get<Components::Transform::WorldMatrix>(entity);
        auto* hierarchy = reg.try_get<Components::Hierarchy::Component>(entity);

        // If no transform, stop recursing this branch (or continue if just hierarchy exists)
        if (!local || !world) return;

        // 2. Determine Dirty State using tag component
        // If parent moved, WE effectively moved in world space, even if our local didn't change.
        bool localDirty = reg.all_of<Components::Transform::IsDirtyTag>(entity);
        bool isDirty = localDirty || parentDirty;

        // 3. Update Matrix if needed
        if (isDirty)
        {
            world->Matrix = parentMatrix * GetMatrix(*local);

            // Emit "world updated" marker for downstream systems (GPUScene sync, physics broadphase, etc.)
            reg.emplace_or_replace<Components::Transform::WorldUpdatedTag>(entity);

            // Remove dirty tag after processing
            reg.remove<Components::Transform::IsDirtyTag>(entity);
        }

        // 4. Recurse Children
        if (hierarchy && hierarchy->FirstChild != entt::null)
        {
            entt::entity child = hierarchy->FirstChild;
            while (child != entt::null)
            {
                // We must fetch the sibling ID *before* recursing, mostly safe,
                // but strictly traversing the list structure.
                auto& childHierarchy = reg.get<Components::Hierarchy::Component>(child);

                // Pass down OUR new world matrix and OUR dirty state
                UpdateHierarchy(reg, child, world->Matrix, isDirty);

                child = childHierarchy.NextSibling;
            }
        }
    }
}

namespace ECS::Systems::Transform
{
    void OnUpdate(entt::registry& registry)
    {
        auto view = registry.view<Components::Transform::Component, Components::Hierarchy::Component>();

        for (auto [entity, transform, hierarchy] : view.each())
        {
            if (hierarchy.Parent == entt::null)
            {
                // Root object: Parent Matrix is Identity
                Detail::UpdateHierarchy(registry, entity, glm::mat4(1.0f), false);
            }
        }
    }
}

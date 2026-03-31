/// Hierarchy public API: composes structural mutations with transform recomputation.
module;

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module ECS:Components.Hierarchy.Impl;
import :Components.Hierarchy;
import :Components.Transform;
import :HierarchyStructure;
import Core.Logging;

namespace ECS::Components::Hierarchy
{
    void Attach(entt::registry& registry, entt::entity child, entt::entity newParent)
    {
        if (!registry.valid(child) || child == newParent) return;

        auto& childComp = registry.get_or_emplace<Component>(child);

        // Handle detach / null parent
        if (newParent == entt::null)
        {
            Detach(registry, child);
            return;
        }

        // Cycle detection
        if (Structure::IsDescendant(registry, child, newParent))
        {
            Core::Log::Warn("Hierarchy::Attach -- cycle detected: cannot attach entity {} to its own descendant {}",
                            static_cast<uint32_t>(child), static_cast<uint32_t>(newParent));
            return;
        }

        // Pre-detach from old parent
        if (childComp.Parent != entt::null)
        {
            if (childComp.Parent == newParent) return; // already done
            Structure::DetachFromParent(registry, childComp);
        }

        // Transform recomputation: preserve world position across reparenting
        const bool childTransformReady = registry.all_of<Transform::Component, Transform::WorldMatrix>(child)
            && !registry.all_of<Transform::IsDirtyTag>(child);
        const bool parentTransformReady = registry.all_of<Transform::WorldMatrix>(newParent)
            && !registry.all_of<Transform::IsDirtyTag>(newParent);

        if (childTransformReady && parentTransformReady)
        {
            auto& childLocal = registry.get<Transform::Component>(child);
            const auto& childWorld = registry.get<Transform::WorldMatrix>(child);
            const auto& parentWorld = registry.get<Transform::WorldMatrix>(newParent);

            if (!Transform::TryComputeLocalTransform(childWorld.Matrix, parentWorld.Matrix, childLocal))
            {
                Core::Log::Warn("Hierarchy::Attach -- matrix decomposition produced NaN "
                                "(singular parent matrix?), keeping original local transform for entity {}",
                                static_cast<uint32_t>(child));
                childLocal.Position = glm::vec3(0.0f);
                childLocal.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childLocal.Scale = glm::vec3(1.0f);
            }

            registry.emplace_or_replace<Transform::IsDirtyTag>(child);
        }
        else if (registry.all_of<Transform::Component>(child))
        {
            registry.emplace_or_replace<Transform::IsDirtyTag>(child);
        }

        // Structural attach
        auto& parentComp = registry.get_or_emplace<Component>(newParent);
        Structure::AttachToParent(registry, child, childComp, newParent, parentComp);
    }

    void Detach(entt::registry& registry, entt::entity child)
    {
        if (!registry.valid(child)) return;

        auto* childComp = registry.try_get<Component>(child);
        if (childComp && childComp->Parent != entt::null)
        {
            Structure::DetachFromParent(registry, *childComp);
        }
    }
}

module;

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

module ECS:Components.Hierarchy.Impl;
import :Components.Hierarchy;
import :Components.Transform;
import :Scene;
import Core.Logging;

namespace ECS::Components::Hierarchy::Detail
{
    using namespace ECS::Components::Hierarchy;

    // Returns true if 'potentialAncestor' is actually a child/grandchild of 'entity'
    bool IsDescendant(entt::registry& registry, entt::entity entity, entt::entity potentialAncestor)
    {
        entt::entity current = potentialAncestor;
        while (registry.valid(current) && current != entt::null)
        {
            if (current == entity) return true;

            // Walk up
            if (auto* comp = registry.try_get<Component>(current))
            {
                current = comp->Parent;
            }
            else
            {
                break; // Hierarchy broken or root reached
            }
        }
        return false;
    }

    void AttachHelper(entt::registry& registry, entt::entity child, Component& childComp,
                      entt::entity parent, Component& parentComp)
    {
        // 1. Set Parent
        childComp.Parent = parent;

        // 2. Insert at Head of Parent's list
        childComp.NextSibling = parentComp.FirstChild;
        childComp.PrevSibling = entt::null; // New head has no prev

        if (registry.valid(parentComp.FirstChild))
        {
            auto& oldHead = registry.get<Component>(parentComp.FirstChild);
            oldHead.PrevSibling = child;
        }

        parentComp.FirstChild = child;
        parentComp.ChildCount++;
    }

    void DetachHelper(entt::registry& registry, Component& childComp)
    {
        entt::entity parent = childComp.Parent;
        auto& parentComp = registry.get<Component>(parent);

        // 1. Fix Previous Sibling or Parent Head
        if (childComp.PrevSibling != entt::null)
        {
            auto& prev = registry.get<Component>(childComp.PrevSibling);
            prev.NextSibling = childComp.NextSibling;
        }
        else
        {
            // If we have no prev, we were the head
            parentComp.FirstChild = childComp.NextSibling;
        }

        // 2. Fix Next Sibling
        if (childComp.NextSibling != entt::null)
        {
            auto& next = registry.get<Component>(childComp.NextSibling);
            next.PrevSibling = childComp.PrevSibling;
        }

        // 3. Update Parent Data
        parentComp.ChildCount--;

        // 4. Clear Child Data
        childComp.Parent = entt::null;
        childComp.NextSibling = entt::null;
        childComp.PrevSibling = entt::null;
    }
}

namespace ECS::Components::Hierarchy
{
    void Attach(entt::registry& registry, entt::entity child, entt::entity newParent)
    {
        if (!registry.valid(child) || child == newParent) return;

        // Ensure both have hierarchy components
        auto& childComp = registry.get_or_emplace<Component>(child);

        // 1. Handle Detachment / Null Parent
        if (newParent == entt::null)
        {
            Detach(registry, child);
            return;
        }

        // 2. Cycle Detection (Crucial!)
        // If we try to parent A to B, but B is a child of A, we create an infinite loop.
        if (Detail::IsDescendant(registry, child, newParent))
        {
            Core::Log::Warn("Hierarchy::Attach -- cycle detected: cannot attach entity {} to its own descendant {}",
                            static_cast<uint32_t>(child), static_cast<uint32_t>(newParent));
            return;
        }

        // 3. If already attached to someone else, detach first
        if (childComp.Parent != entt::null)
        {
            if (childComp.Parent == newParent) return; // Already done
            Detail::DetachHelper(registry, childComp);
        }

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
            // Parent/child world matrices may still be stale this tick. In that case,
            // preserve the current local transform and let the Transform system rebuild
            // world-space from the new hierarchy on the next update.
            registry.emplace_or_replace<Transform::IsDirtyTag>(child);
        }

        // 4. Perform Attach
        // Ensure parent has component
        auto& parentComp = registry.get_or_emplace<Component>(newParent);
        Detail::AttachHelper(registry, child, childComp, newParent, parentComp);
    }

    void Detach(entt::registry& registry, entt::entity child)
    {
        if (!registry.valid(child)) return;

        // Use try_get. If it doesn't have a component, it's effectively detached.
        auto* childComp = registry.try_get<Component>(child);
        if (childComp && childComp->Parent != entt::null)
        {
            Detail::DetachHelper(registry, *childComp);
        }
    }
}

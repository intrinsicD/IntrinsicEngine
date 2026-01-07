module;
#include <entt/entity/registry.hpp>
#include <cassert>

export module ECS:Components.Hierarchy;

export namespace ECS::Components::Hierarchy
{
    struct Component
    {
        entt::entity Parent = entt::null;
        entt::entity FirstChild = entt::null;
        entt::entity NextSibling = entt::null;
        entt::entity PrevSibling = entt::null;
        uint32_t ChildCount = 0;
    };

    // Forward declarations of API functions
    void Attach(entt::registry& registry, entt::entity child, entt::entity newParent);
    void Detach(entt::registry& registry, entt::entity child);
}

// -----------------------------------------------------------------------------
// IMPLEMENTATION DETAILS (Not Exported)
// -----------------------------------------------------------------------------
// In a pure module setup, this might go in a separate .cpp (Implementation Unit),
// but keeping it here for simplicity is fine, just don't export the namespace.
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

    void DetachHelper(entt::registry& registry, entt::entity child, Component& childComp)
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

// -----------------------------------------------------------------------------
// PUBLIC API IMPLEMENTATION
// -----------------------------------------------------------------------------
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
            // Log Error: "Cannot attach entity to its own descendant"
            return;
        }

        // 3. If already attached to someone else, detach first
        if (childComp.Parent != entt::null)
        {
            if (childComp.Parent == newParent) return; // Already done
            Detail::DetachHelper(registry, child, childComp);
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
            Detail::DetachHelper(registry, child, *childComp);
        }
    }
}

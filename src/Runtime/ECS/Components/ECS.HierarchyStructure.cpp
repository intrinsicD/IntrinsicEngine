/// Pure structural hierarchy mutations — no Transform dependency.
module;

#include <entt/entity/registry.hpp>
#include <cstdint>

module ECS:HierarchyStructure.Impl;
import :HierarchyStructure;
import :Components.Hierarchy;

namespace ECS::Components::Hierarchy::Structure
{
    bool IsDescendant(entt::registry& registry, entt::entity entity, entt::entity potentialDescendant)
    {
        entt::entity current = potentialDescendant;
        uint32_t safety = 0;
        while (registry.valid(current) && current != entt::null)
        {
            if (current == entity) return true;
            if (++safety > kMaxAncestryDepth) return false; // bounded walk

            if (auto* comp = registry.try_get<Component>(current))
            {
                current = comp->Parent;
            }
            else
            {
                break;
            }
        }
        return false;
    }

    void AttachToParent(entt::registry& registry, entt::entity child, Component& childComp,
                        entt::entity parent, Component& parentComp)
    {
        childComp.Parent = parent;
        childComp.NextSibling = parentComp.FirstChild;
        childComp.PrevSibling = entt::null;

        if (registry.valid(parentComp.FirstChild))
        {
            auto& oldHead = registry.get<Component>(parentComp.FirstChild);
            oldHead.PrevSibling = child;
        }

        parentComp.FirstChild = child;
        parentComp.ChildCount++;
    }

    void DetachFromParent(entt::registry& registry, Component& childComp)
    {
        entt::entity parent = childComp.Parent;

        auto* parentComp = registry.valid(parent) ? registry.try_get<Component>(parent) : nullptr;

        if (parentComp)
        {
            if (childComp.PrevSibling != entt::null)
            {
                if (auto* prev = registry.try_get<Component>(childComp.PrevSibling))
                    prev->NextSibling = childComp.NextSibling;
            }
            else
            {
                parentComp->FirstChild = childComp.NextSibling;
            }

            if (childComp.NextSibling != entt::null)
            {
                if (auto* next = registry.try_get<Component>(childComp.NextSibling))
                    next->PrevSibling = childComp.PrevSibling;
            }

            parentComp->ChildCount--;
        }

        childComp.Parent = entt::null;
        childComp.NextSibling = entt::null;
        childComp.PrevSibling = entt::null;
    }

    bool ValidateInvariants(const entt::registry& registry, entt::entity entity)
    {
        if (!registry.valid(entity)) return false;

        auto* comp = registry.try_get<Component>(entity);
        if (!comp) return true; // no hierarchy component is valid (detached)

        // Invariant: Parent != self
        if (comp->Parent == entity) return false;

        // Invariant: not our own sibling
        if (comp->NextSibling == entity || comp->PrevSibling == entity) return false;

        // Invariant: ChildCount matches linked list length
        uint32_t counted = 0;
        entt::entity child = comp->FirstChild;
        uint32_t safety = 0;
        while (registry.valid(child) && child != entt::null)
        {
            if (++safety > kMaxAncestryDepth) return false; // loop detected
            ++counted;
            auto* childComp = registry.try_get<Component>(child);
            if (!childComp) return false;
            if (childComp->Parent != entity) return false;
            child = childComp->NextSibling;
        }

        return counted == comp->ChildCount;
    }
}

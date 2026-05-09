module;

#include <cstdint>
#include <entt/entity/registry.hpp>

module Extrinsic.ECS.Hierarchy.Structure;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.Hierarchy;

namespace Extrinsic::ECS::Hierarchy::Structure
{
    bool IsDescendant(const entt::registry& registry,
                      const EntityHandle entity,
                      const EntityHandle potentialDescendant) noexcept
    {
        EntityHandle current = potentialDescendant;
        std::uint32_t safety = 0;
        while (registry.valid(current) && current != InvalidEntityHandle)
        {
            if (current == entity) return true;
            if (++safety > kMaxAncestryDepth) return false;

            if (const auto* comp = registry.try_get<Component>(current))
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

    void AttachToParent(entt::registry& registry,
                        const EntityHandle child,
                        Component& childComp,
                        const EntityHandle parent,
                        Component& parentComp) noexcept
    {
        childComp.Parent = parent;
        childComp.NextSibling = parentComp.FirstChild;
        childComp.PrevSibling = InvalidEntityHandle;

        if (registry.valid(parentComp.FirstChild))
        {
            auto& oldHead = registry.get<Component>(parentComp.FirstChild);
            oldHead.PrevSibling = child;
        }

        parentComp.FirstChild = child;
        parentComp.ChildCount++;
    }

    void DetachFromParent(entt::registry& registry, Component& childComp) noexcept
    {
        const EntityHandle parent = childComp.Parent;
        auto* parentComp = registry.valid(parent) ? registry.try_get<Component>(parent) : nullptr;

        if (parentComp != nullptr)
        {
            if (childComp.PrevSibling != InvalidEntityHandle)
            {
                if (auto* prev = registry.try_get<Component>(childComp.PrevSibling))
                {
                    prev->NextSibling = childComp.NextSibling;
                }
            }
            else
            {
                parentComp->FirstChild = childComp.NextSibling;
            }

            if (childComp.NextSibling != InvalidEntityHandle)
            {
                if (auto* next = registry.try_get<Component>(childComp.NextSibling))
                {
                    next->PrevSibling = childComp.PrevSibling;
                }
            }

            if (parentComp->ChildCount > 0u)
            {
                parentComp->ChildCount--;
            }
        }

        childComp.Parent = InvalidEntityHandle;
        childComp.NextSibling = InvalidEntityHandle;
        childComp.PrevSibling = InvalidEntityHandle;
    }

    bool ValidateInvariants(const entt::registry& registry, const EntityHandle entity) noexcept
    {
        if (!registry.valid(entity)) return false;

        const auto* comp = registry.try_get<Component>(entity);
        if (comp == nullptr) return true;

        if (comp->Parent == entity) return false;
        if (comp->NextSibling == entity || comp->PrevSibling == entity) return false;

        std::uint32_t counted = 0u;
        EntityHandle child = comp->FirstChild;
        std::uint32_t safety = 0u;
        while (registry.valid(child) && child != InvalidEntityHandle)
        {
            if (++safety > kMaxAncestryDepth) return false;
            ++counted;
            const auto* childComp = registry.try_get<Component>(child);
            if (childComp == nullptr) return false;
            if (childComp->Parent != entity) return false;
            child = childComp->NextSibling;
        }

        return counted == comp->ChildCount;
    }
}

module;

#include <cstdint>
#include <unordered_set>
#include <vector>
#include <entt/entity/registry.hpp>

module Extrinsic.ECS.Hierarchy.Structure;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.Hierarchy;

namespace Extrinsic::ECS::Hierarchy::Structure
{
    namespace
    {
        [[nodiscard]] HierarchyQueryStatus CollectCheckedChildChain(
            const entt::registry& registry,
            const EntityHandle root,
            std::vector<EntityHandle>& output)
        {
            output.clear();
            if (root == InvalidEntityHandle || !registry.valid(root))
                return HierarchyQueryStatus::InvalidRoot;

            const auto* hierarchy = registry.try_get<Component>(root);
            if (hierarchy == nullptr)
                return HierarchyQueryStatus::Success;

            const bool hasFirstChild =
                hierarchy->FirstChild != InvalidEntityHandle;
            if (hasFirstChild != (hierarchy->ChildCount != 0u))
                return HierarchyQueryStatus::ChildCountMismatch;

            std::unordered_set<EntityHandle> visited{};
            EntityHandle previous = InvalidEntityHandle;
            EntityHandle child = hierarchy->FirstChild;
            while (child != InvalidEntityHandle)
            {
                if (!registry.valid(child))
                {
                    output.clear();
                    return HierarchyQueryStatus::DanglingLink;
                }
                if (visited.contains(child))
                {
                    output.clear();
                    return HierarchyQueryStatus::DuplicateOrCycle;
                }
                if (output.size() >= kMaxHierarchyQueryEntities)
                {
                    output.clear();
                    return HierarchyQueryStatus::TraversalLimitExceeded;
                }
                visited.insert(child);

                const auto* childHierarchy = registry.try_get<Component>(child);
                if (childHierarchy == nullptr)
                {
                    output.clear();
                    return HierarchyQueryStatus::MissingChildHierarchy;
                }
                if (childHierarchy->Parent != root)
                {
                    output.clear();
                    return HierarchyQueryStatus::ParentMismatch;
                }
                if (childHierarchy->PrevSibling != previous)
                {
                    output.clear();
                    return HierarchyQueryStatus::SiblingBacklinkMismatch;
                }

                output.push_back(child);
                previous = child;
                child = childHierarchy->NextSibling;
            }

            if (output.size() != hierarchy->ChildCount)
            {
                output.clear();
                return HierarchyQueryStatus::ChildCountMismatch;
            }
            return HierarchyQueryStatus::Success;
        }

        [[nodiscard]] HierarchyQueryStatus ScheduleChildren(
            const std::vector<EntityHandle>& children,
            std::unordered_set<EntityHandle>& discovered,
            std::vector<EntityHandle>& stack)
        {
            for (const EntityHandle child : children)
            {
                if (discovered.contains(child))
                    return HierarchyQueryStatus::DuplicateOrCycle;
                if (discovered.size() - 1u >= kMaxHierarchyQueryEntities)
                    return HierarchyQueryStatus::TraversalLimitExceeded;
                discovered.insert(child);
            }

            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(*it);
            return HierarchyQueryStatus::Success;
        }

        [[nodiscard]] HierarchyQueryResult FailedQuery(
            const HierarchyQueryStatus status)
        {
            return HierarchyQueryResult{.Status = status};
        }
    }

    const char* DebugNameForHierarchyQueryStatus(
        const HierarchyQueryStatus status) noexcept
    {
        switch (status)
        {
        case HierarchyQueryStatus::Success:
            return "Success";
        case HierarchyQueryStatus::InvalidRoot:
            return "InvalidRoot";
        case HierarchyQueryStatus::DanglingLink:
            return "DanglingLink";
        case HierarchyQueryStatus::MissingChildHierarchy:
            return "MissingChildHierarchy";
        case HierarchyQueryStatus::ParentMismatch:
            return "ParentMismatch";
        case HierarchyQueryStatus::SiblingBacklinkMismatch:
            return "SiblingBacklinkMismatch";
        case HierarchyQueryStatus::ChildCountMismatch:
            return "ChildCountMismatch";
        case HierarchyQueryStatus::DuplicateOrCycle:
            return "DuplicateOrCycle";
        case HierarchyQueryStatus::TraversalLimitExceeded:
            return "TraversalLimitExceeded";
        }
        return "Unknown";
    }

    HierarchyQueryResult CollectChildren(
        const entt::registry& registry,
        const EntityHandle root)
    {
        HierarchyQueryResult result{};
        result.Status =
            CollectCheckedChildChain(registry, root, result.Entities);
        if (!result.Succeeded())
            result.Entities.clear();
        return result;
    }

    HierarchyQueryResult CollectDescendantsPreorder(
        const entt::registry& registry,
        const EntityHandle root)
    {
        if (root == InvalidEntityHandle || !registry.valid(root))
            return FailedQuery(HierarchyQueryStatus::InvalidRoot);

        std::vector<EntityHandle> children{};
        const HierarchyQueryStatus rootStatus =
            CollectCheckedChildChain(registry, root, children);
        if (rootStatus != HierarchyQueryStatus::Success)
            return FailedQuery(rootStatus);

        HierarchyQueryResult result{};
        std::vector<EntityHandle> stack{};
        std::unordered_set<EntityHandle> discovered{};
        discovered.insert(root);
        const HierarchyQueryStatus rootScheduleStatus =
            ScheduleChildren(children, discovered, stack);
        if (rootScheduleStatus != HierarchyQueryStatus::Success)
            return FailedQuery(rootScheduleStatus);

        while (!stack.empty())
        {
            const EntityHandle entity = stack.back();
            stack.pop_back();
            result.Entities.push_back(entity);

            const HierarchyQueryStatus childStatus =
                CollectCheckedChildChain(registry, entity, children);
            if (childStatus != HierarchyQueryStatus::Success)
                return FailedQuery(childStatus);

            const HierarchyQueryStatus scheduleStatus =
                ScheduleChildren(children, discovered, stack);
            if (scheduleStatus != HierarchyQueryStatus::Success)
                return FailedQuery(scheduleStatus);
        }
        return result;
    }

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

    bool ValidateInvariants(const entt::registry& registry, const EntityHandle entity)
    {
        if (!registry.valid(entity)) return false;

        const auto* comp = registry.try_get<Component>(entity);
        if (comp == nullptr) return true;

        if (comp->Parent == entity) return false;
        if (comp->NextSibling == entity || comp->PrevSibling == entity) return false;

        std::vector<EntityHandle> children{};
        return CollectCheckedChildChain(registry, entity, children) ==
               HierarchyQueryStatus::Success;
    }
}

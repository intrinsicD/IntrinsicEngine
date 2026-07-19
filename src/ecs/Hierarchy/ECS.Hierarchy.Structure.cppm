module;

#include <cstdint>
#include <vector>
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Hierarchy.Structure;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.Hierarchy;

export namespace Extrinsic::ECS::Hierarchy::Structure
{
    // Bound on ancestor walks; corruption guard rather than scene policy.
    inline constexpr std::uint32_t kMaxAncestryDepth = 65536u;
    // Bound on entities inspected by hierarchy collection queries.
    inline constexpr std::uint32_t kMaxHierarchyQueryEntities =
        kMaxAncestryDepth;

    using Components::Hierarchy::Component;

    enum class HierarchyQueryStatus : std::uint8_t
    {
        Success,
        InvalidRoot,
        DanglingLink,
        MissingChildHierarchy,
        ParentMismatch,
        SiblingBacklinkMismatch,
        ChildCountMismatch,
        DuplicateOrCycle,
        TraversalLimitExceeded,
    };

    struct HierarchyQueryResult
    {
        HierarchyQueryStatus Status{HierarchyQueryStatus::Success};
        std::vector<EntityHandle> Entities{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == HierarchyQueryStatus::Success;
        }
    };

    [[nodiscard]] const char* DebugNameForHierarchyQueryStatus(
        HierarchyQueryStatus status) noexcept;

    // Exact FirstChild/NextSibling order. Failures never retain a prefix.
    [[nodiscard]] HierarchyQueryResult CollectChildren(
        const entt::registry& registry,
        EntityHandle root);

    // Iterative depth-first preorder, excluding root. Failures never retain
    // a prefix.
    [[nodiscard]] HierarchyQueryResult CollectDescendantsPreorder(
        const entt::registry& registry,
        EntityHandle root);

    [[nodiscard]] bool IsDescendant(const entt::registry& registry,
                                    EntityHandle entity,
                                    EntityHandle potentialDescendant) noexcept;

    void AttachToParent(entt::registry& registry,
                        EntityHandle child,
                        Component& childComp,
                        EntityHandle parent,
                        Component& parentComp) noexcept;

    void DetachFromParent(entt::registry& registry, Component& childComp) noexcept;

    [[nodiscard]] bool ValidateInvariants(const entt::registry& registry,
                                          EntityHandle entity);
}

/// Private partition: pure structural hierarchy mutations.
/// No transform imports — only linked-list manipulation and cycle detection.
module;

#include <entt/entity/registry.hpp>
#include <cstdint>

export module ECS:HierarchyStructure;
import :Components.Hierarchy;

export namespace ECS::Components::Hierarchy::Structure
{
    /// Maximum ancestor-walk depth before declaring corruption and bailing out.
    /// 65536 is generous for any real scene (skeletal chains rarely exceed 200).
    inline constexpr uint32_t kMaxAncestryDepth = 65536;

    /// Returns true if 'entity' is an ancestor of 'potentialDescendant'.
    /// Walk-up bounded by kMaxAncestryDepth to prevent infinite loops on corrupted data.
    [[nodiscard]] bool IsDescendant(entt::registry& registry, entt::entity entity,
                                    entt::entity potentialDescendant);

    /// Insert child at head of parent's child list. Pure structural — no transform logic.
    void AttachToParent(entt::registry& registry, entt::entity child, Component& childComp,
                        entt::entity parent, Component& parentComp);

    /// Remove child from its parent's child list. Pure structural — no transform logic.
    /// Handles partial destruction (parent component may already be removed).
    void DetachFromParent(entt::registry& registry, Component& childComp);

    /// Validate hierarchy invariants on a single entity.
    /// Checks: Parent != self, no self-sibling loops, ChildCount consistency.
    /// Returns true if all invariants hold.
    [[nodiscard]] bool ValidateInvariants(const entt::registry& registry, entt::entity entity);
}

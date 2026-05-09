module;

#include <cstdint>
#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Hierarchy.Structure;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.Hierarchy;

export namespace Extrinsic::ECS::Hierarchy::Structure
{
    // Bound on ancestor walks; corruption guard rather than scene policy.
    inline constexpr std::uint32_t kMaxAncestryDepth = 65536u;

    using Components::Hierarchy::Component;

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
                                          EntityHandle entity) noexcept;
}

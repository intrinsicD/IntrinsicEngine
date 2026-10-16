module;

#include <cmath>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.ECS.Hierarchy.Mutation;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Hierarchy.Structure;

namespace Extrinsic::ECS::Hierarchy
{
    namespace Components = ::Extrinsic::ECS::Components;

    namespace
    {
        bool IsFiniteMatrix(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row]))
                        return false;
            return true;
        }
    }

    void Detach(entt::registry& registry, const EntityHandle child)
    {
        if (!registry.valid(child)) return;
        auto* hierarchy = registry.try_get<Components::Hierarchy::Component>(child);
        if (hierarchy == nullptr || hierarchy->Parent == InvalidEntityHandle) return;

        auto* local = registry.try_get<Components::Transform::Component>(child);
        const auto* world = registry.try_get<Components::Transform::WorldMatrix>(child);
        if (local != nullptr && world != nullptr &&
            !registry.all_of<Components::Transform::IsDirtyTag>(child))
        {
            // Teardown repeatedly detaches FirstChild and requires progress.
            // Extracting TRS preserves translation while necessarily dropping shear;
            // failed decomposition leaves the authored local untouched.
            Components::Transform::Component candidate;
            if (Components::Transform::TryDecomposeMatrix(world->Matrix, candidate))
                *local = candidate;
        }

        Structure::DetachFromParent(registry, *hierarchy);
        if (local != nullptr)
            registry.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
    }

    void Attach(entt::registry& registry, const EntityHandle child, const EntityHandle newParent)
    {
        if (!registry.valid(child) || child == newParent) return;
        if (newParent == InvalidEntityHandle)
        {
            Detach(registry, child);
            return;
        }
        if (!registry.valid(newParent)) return;
        if (Structure::IsDescendant(registry, child, newParent)) return;
        if (const auto* hierarchy = registry.try_get<Components::Hierarchy::Component>(child);
            hierarchy != nullptr && hierarchy->Parent == newParent)
            return;

        const bool childTransformReady =
            registry.all_of<Components::Transform::Component, Components::Transform::WorldMatrix>(child)
            && !registry.all_of<Components::Transform::IsDirtyTag>(child);
        const bool parentTransformReady =
            registry.all_of<Components::Transform::WorldMatrix>(newParent)
            && !registry.all_of<Components::Transform::IsDirtyTag>(newParent);
        Components::Transform::Component candidate;
        if (childTransformReady && parentTransformReady)
        {
            const auto& childWorld = registry.get<Components::Transform::WorldMatrix>(child).Matrix;
            const auto& parentWorld = registry.get<Components::Transform::WorldMatrix>(newParent).Matrix;
            if (!IsFiniteMatrix(childWorld) || !IsFiniteMatrix(parentWorld)) return;
            const float determinant = glm::determinant(parentWorld);
            if (!std::isfinite(determinant)) return;
            // Preserve the existing finite singular-parent identity policy.
            // Other failures (including required shear) reject before unlinking.
            if (std::abs(determinant) >= 1.0e-8f &&
                !Components::Transform::TryComputeLocalTransform(childWorld, parentWorld, candidate))
                return;
        }

        auto& childComp = registry.get_or_emplace<Components::Hierarchy::Component>(child);
        if (childComp.Parent != InvalidEntityHandle)
            Structure::DetachFromParent(registry, childComp);
        if (childTransformReady && parentTransformReady)
            registry.get<Components::Transform::Component>(child) = candidate;
        if (registry.all_of<Components::Transform::Component>(child))
            registry.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
        auto& parentComp = registry.get_or_emplace<Components::Hierarchy::Component>(newParent);
        Structure::AttachToParent(registry, child, childComp, newParent, parentComp);
    }
}

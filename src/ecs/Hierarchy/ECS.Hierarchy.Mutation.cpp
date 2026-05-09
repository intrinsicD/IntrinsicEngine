module;

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

    void Detach(entt::registry& registry, const EntityHandle child)
    {
        if (!registry.valid(child)) return;

        auto* childComp = registry.try_get<Components::Hierarchy::Component>(child);
        if (childComp != nullptr && childComp->Parent != InvalidEntityHandle)
        {
            Structure::DetachFromParent(registry, *childComp);
        }
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

        auto& childComp = registry.get_or_emplace<Components::Hierarchy::Component>(child);

        if (Structure::IsDescendant(registry, child, newParent))
        {
            return;
        }

        if (childComp.Parent == newParent) return;

        if (childComp.Parent != InvalidEntityHandle)
        {
            Structure::DetachFromParent(registry, childComp);
        }

        const bool childTransformReady =
            registry.all_of<Components::Transform::Component, Components::Transform::WorldMatrix>(child)
            && !registry.all_of<Components::Transform::IsDirtyTag>(child);
        const bool parentTransformReady =
            registry.all_of<Components::Transform::WorldMatrix>(newParent)
            && !registry.all_of<Components::Transform::IsDirtyTag>(newParent);

        if (childTransformReady && parentTransformReady)
        {
            auto& childLocal = registry.get<Components::Transform::Component>(child);
            const auto& childWorld = registry.get<Components::Transform::WorldMatrix>(child);
            const auto& parentWorld = registry.get<Components::Transform::WorldMatrix>(newParent);

            if (!Components::Transform::TryComputeLocalTransform(childWorld.Matrix, parentWorld.Matrix, childLocal))
            {
                childLocal.Position = glm::vec3(0.0f);
                childLocal.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childLocal.Scale = glm::vec3(1.0f);
            }

            registry.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
        }
        else if (registry.all_of<Components::Transform::Component>(child))
        {
            registry.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
        }

        auto& parentComp = registry.get_or_emplace<Components::Hierarchy::Component>(newParent);
        Structure::AttachToParent(registry, child, childComp, newParent, parentComp);
    }
}

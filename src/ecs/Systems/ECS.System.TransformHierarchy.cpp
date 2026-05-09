module;

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.ECS.System.TransformHierarchy;

import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;

namespace Extrinsic::ECS::Systems::TransformHierarchy
{
    namespace Components = ::Extrinsic::ECS::Components;

    namespace
    {
        void UpdateNode(entt::registry& registry,
                        const EntityHandle entity,
                        const glm::mat4& parentMatrix,
                        const bool parentDirty)
        {
            auto* local = registry.try_get<Components::Transform::Component>(entity);
            auto* world = registry.try_get<Components::Transform::WorldMatrix>(entity);
            const auto* hierarchy = registry.try_get<Components::Hierarchy::Component>(entity);

            if (local == nullptr || world == nullptr)
            {
                return;
            }

            const bool localDirty = registry.all_of<Components::Transform::IsDirtyTag>(entity);
            const bool isDirty = localDirty || parentDirty;

            if (isDirty)
            {
                world->Matrix = parentMatrix * Components::Transform::GetMatrix(*local);
                registry.emplace_or_replace<Components::Transform::WorldUpdatedTag>(entity);
                registry.remove<Components::Transform::IsDirtyTag>(entity);
            }

            if (hierarchy == nullptr || hierarchy->FirstChild == InvalidEntityHandle)
            {
                return;
            }

            EntityHandle child = hierarchy->FirstChild;
            while (child != InvalidEntityHandle)
            {
                const auto& childHierarchy = registry.get<Components::Hierarchy::Component>(child);
                const EntityHandle next = childHierarchy.NextSibling;
                UpdateNode(registry, child, world->Matrix, isDirty);
                child = next;
            }
        }
    }

    void OnUpdate(entt::registry& registry)
    {
        // Roots own the traversal. Iterating over the joint
        // (Transform, Hierarchy) view ensures we only visit entities that
        // can actually contribute to a recompute.
        const auto roots = registry.view<Components::Transform::Component,
                                         Components::Hierarchy::Component>();
        for (auto [entity, transform, hierarchy] : roots.each())
        {
            (void)transform;
            if (hierarchy.Parent == InvalidEntityHandle)
            {
                UpdateNode(registry, entity, glm::mat4(1.0f), false);
            }
        }
    }

    void RegisterSystem(Extrinsic::Core::FrameGraph& graph, entt::registry& registry)
    {
        graph.AddPass(PassName,
            [](Extrinsic::Core::FrameGraphBuilder& builder)
            {
                builder.Read<Components::Transform::Component>();
                builder.Read<Components::Hierarchy::Component>();
                builder.Write<Components::Transform::WorldMatrix>();
                builder.Write<Components::Transform::IsDirtyTag>();
                builder.Write<Components::Transform::WorldUpdatedTag>();
                builder.Signal(Extrinsic::Core::Hash::StringID{PassName});
            },
            [&registry]()
            {
                OnUpdate(registry);
            });
    }
}

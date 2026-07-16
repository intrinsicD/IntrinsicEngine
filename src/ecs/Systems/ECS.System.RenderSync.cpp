module;

#include <cstdint>

#include <entt/entity/registry.hpp>

module Extrinsic.ECS.System.RenderSync;

import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.System.BoundsPropagation;
import Extrinsic.ECS.System.TransformHierarchy;

namespace Extrinsic::ECS::Systems::RenderSync
{
    namespace Components = ::Extrinsic::ECS::Components;

    void OnUpdate(entt::registry& registry)
    {
        Stats discard{};
        OnUpdate(registry, discard);
    }

    void OnUpdate(entt::registry& registry, Stats& stats)
    {
        const auto view = registry.view<Components::Transform::WorldUpdatedTag>();

        // emplace_or_replace on a different component family does not
        // invalidate iteration over WorldUpdatedTag's storage.
        for (const auto entity : view)
        {
            ++stats.WorldUpdatedObserved;
            registry.emplace_or_replace<Components::DirtyTags::DirtyTransform>(entity);
            ++stats.DirtyTransformStamped;
        }

        // Bulk-clear the producer signal once forwarding completes; only
        // TransformHierarchy stamps WorldUpdatedTag in promoted code, and
        // the FrameGraph WaitFor edges below ensure it has already run.
        const auto cleared = registry.storage<Components::Transform::WorldUpdatedTag>().size();
        stats.WorldUpdatedCleared += static_cast<std::uint32_t>(cleared);
        registry.clear<Components::Transform::WorldUpdatedTag>();
    }

    void RegisterSystem(Extrinsic::Core::FrameGraph& graph, entt::registry& registry)
    {
        graph.AddPass(PassName,
            [](Extrinsic::Core::FrameGraphBuilder& builder)
            {
                // DirtyTransform is materialized on demand and
                // WorldUpdatedTag is cleared, so this pass mutates registry
                // structure in addition to its component-specific writes.
                builder.StructuralWrite();
                builder.WaitFor(Extrinsic::Core::Hash::StringID{TransformHierarchy::PassName});
                builder.WaitFor(Extrinsic::Core::Hash::StringID{BoundsPropagation::PassName});
                builder.Write<Components::Transform::WorldUpdatedTag>();
                builder.Write<Components::DirtyTags::DirtyTransform>();
                builder.Signal(Extrinsic::Core::Hash::StringID{PassName});
            },
            [&registry]()
            {
                OnUpdate(registry);
            });
    }
}

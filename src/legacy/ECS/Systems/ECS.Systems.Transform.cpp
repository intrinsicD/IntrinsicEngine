module;

#include <entt/entity/registry.hpp>

module ECS:Systems.Transform.Impl;

import :Systems.Transform;
import :TransformGraphContracts;
import :HierarchyTraversal;
import Core.FrameGraph;

namespace ECS::Systems::Transform
{
    void OnUpdate(entt::registry& registry)
    {
        HierarchyTraversal::UpdateWorldTransforms(registry);
    }

    void RegisterSystem(Core::FrameGraph& graph, entt::registry& registry)
    {
        graph.AddPass(Contracts::PassName,
            [](Core::FrameGraphBuilder& builder)
            {
                Contracts::DeclareTransformPass(builder);
            },
            [&registry]()
            {
                OnUpdate(registry);
            });
    }
}

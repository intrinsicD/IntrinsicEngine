module;

#include <cstdint>

module Extrinsic.Runtime.EcsSystemBundle;

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.System.BoundsPropagation;
import Extrinsic.ECS.System.RenderSync;
import Extrinsic.ECS.System.TransformHierarchy;

namespace Extrinsic::Runtime
{
    PromotedEcsSystemBundleStats RegisterPromotedEcsSystemBundle(
        Core::FrameGraph& graph,
        ECS::Scene::Registry& scene)
    {
        PromotedEcsSystemBundleStats stats{};

        ECS::Systems::TransformHierarchy::RegisterSystem(graph, scene.Raw());
        stats.TransformHierarchyRegistered = true;
        ++stats.Registered;

        ECS::Systems::BoundsPropagation::RegisterSystem(graph, scene.Raw());
        stats.BoundsPropagationRegistered = true;
        ++stats.Registered;

        ECS::Systems::RenderSync::RegisterSystem(graph, scene.Raw());
        stats.RenderSyncRegistered = true;
        ++stats.Registered;

        return stats;
    }
}

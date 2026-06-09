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

    PreRenderTransformFlushStats FlushPreRenderTransformState(
        ECS::Scene::Registry& scene)
    {
        // Same system order the FrameGraph resolves for the fixed-step
        // bundle: world matrices first, then bounds (which read
        // WorldUpdatedTag), then the RenderSync tag forwarding (which
        // clears WorldUpdatedTag and stamps DirtyTransform).
        ECS::Systems::TransformHierarchy::OnUpdate(scene.Raw());
        ECS::Systems::BoundsPropagation::OnUpdate(scene.Raw());

        ECS::Systems::RenderSync::Stats renderSync{};
        ECS::Systems::RenderSync::OnUpdate(scene.Raw(), renderSync);

        return PreRenderTransformFlushStats{
            .WorldUpdatedObserved = renderSync.WorldUpdatedObserved,
            .DirtyTransformStamped = renderSync.DirtyTransformStamped,
            .WorldUpdatedCleared = renderSync.WorldUpdatedCleared,
        };
    }
}

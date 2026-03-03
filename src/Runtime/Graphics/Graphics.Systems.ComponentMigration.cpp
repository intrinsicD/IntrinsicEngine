module;

#include <entt/entity/registry.hpp>

module Graphics:Systems.ComponentMigration.Impl;

import :Components;
import Core.FrameGraph;
import Core.Hash;

using namespace Core::Hash;

namespace Graphics::Systems::ComponentMigration
{

void OnUpdate(entt::registry& registry)
{
    // -----------------------------------------------------------------------
    // PointCloudRenderer → Point
    // -----------------------------------------------------------------------
    // The only remaining bridge. Graph::Data → Line+Point and
    // PointCloud::Data → Point bridging moved to their respective
    // lifecycle systems (GraphGeometrySyncSystem, PointCloudGeometrySyncSystem)
    // in Phase 6.
    {
        auto view = registry.view<ECS::PointCloudRenderer::Component>();
        for (auto entity : view)
        {
            const auto& pc = view.get<ECS::PointCloudRenderer::Component>(entity);
            if (!pc.Visible) continue;

            auto& pt = registry.get_or_emplace<ECS::Point::Component>(entity);
            pt.Geometry        = pc.Geometry;
            pt.Color           = pc.DefaultColor;
            pt.Size            = pc.DefaultRadius;
            pt.SizeMultiplier  = pc.SizeMultiplier;
            pt.Mode            = pc.RenderMode;
            pt.HasPerPointColors  = pc.HasColors();
            pt.HasPerPointRadii   = pc.HasRadii();
            pt.HasPerPointNormals = pc.HasNormals();
        }
    }
}

void RegisterSystem(Core::FrameGraph& graph,
                    entt::registry& registry)
{
    graph.AddPass("ComponentMigration",
        [](Core::FrameGraphBuilder& builder)
        {
            // Only PointCloudRenderer bridging remains after Phase 6.
            builder.WaitFor("PointCloudRendererLifecycle"_id);
            builder.Signal("ComponentMigration"_id);
        },
        [&registry]()
        {
            OnUpdate(registry);
        });
}

} // namespace Graphics::Systems::ComponentMigration

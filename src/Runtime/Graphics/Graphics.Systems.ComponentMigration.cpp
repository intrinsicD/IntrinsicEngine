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
    // 3. PointCloudRenderer → Point
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // 4. Graph::Data → Line + Point
    // -----------------------------------------------------------------------
    {
        auto view = registry.view<ECS::Graph::Data>();
        for (auto entity : view)
        {
            const auto& gd = view.get<ECS::Graph::Data>(entity);
            if (!gd.Visible) continue;

            // Edges → Line (edge index buffer via ReuseVertexBuffersFrom)
            {
                auto& line = registry.get_or_emplace<ECS::Line::Component>(entity);
                line.Geometry         = gd.GpuGeometry;
                line.EdgeView         = gd.GpuEdgeGeometry;
                line.EdgeCount        = gd.GpuEdgeCount;
                line.Color            = gd.DefaultEdgeColor;
                line.Width            = gd.EdgeWidth;
                line.Overlay          = gd.EdgesOverlay;
                line.HasPerEdgeColors = !gd.CachedEdgeColors.empty();
            }

            // Nodes → Point
            {
                auto& pt = registry.get_or_emplace<ECS::Point::Component>(entity);
                pt.Geometry          = gd.GpuGeometry;
                pt.Color             = gd.DefaultNodeColor;
                pt.Size              = gd.DefaultNodeRadius;
                pt.SizeMultiplier    = gd.NodeSizeMultiplier;
                pt.Mode              = gd.NodeRenderMode;
                pt.HasPerPointColors = !gd.CachedNodeColors.empty();
                pt.HasPerPointRadii  = !gd.CachedNodeRadii.empty();
            }
        }
    }

    // -----------------------------------------------------------------------
    // 5. PointCloud::Data → Point
    // -----------------------------------------------------------------------
    {
        auto view = registry.view<ECS::PointCloud::Data>();
        for (auto entity : view)
        {
            const auto& pcd = view.get<ECS::PointCloud::Data>(entity);
            if (!pcd.Visible) continue;

            auto& pt = registry.get_or_emplace<ECS::Point::Component>(entity);
            pt.Geometry        = pcd.GpuGeometry;
            pt.Color           = pcd.DefaultColor;
            pt.Size            = pcd.DefaultRadius;
            pt.SizeMultiplier  = pcd.SizeMultiplier;
            pt.Mode            = pcd.RenderMode;
            pt.HasPerPointColors  = pcd.HasColors();
            pt.HasPerPointRadii   = pcd.HasRadii();
            pt.HasPerPointNormals = pcd.HasNormals();
        }
    }
}

void RegisterSystem(Core::FrameGraph& graph,
                    entt::registry& registry)
{
    graph.AddPass("ComponentMigration",
        [](Core::FrameGraphBuilder& builder)
        {
            // Run after all legacy lifecycle systems so GPU state is populated.
            builder.WaitFor("TransformUpdate"_id);
            builder.WaitFor("MeshRendererLifecycle"_id);
            builder.WaitFor("PointCloudRendererLifecycle"_id);
            builder.WaitFor("GraphGeometrySync"_id);
            builder.WaitFor("PointCloudGeometrySync"_id);
            builder.Signal("ComponentMigration"_id);
        },
        [&registry]()
        {
            OnUpdate(registry);
        });
}

} // namespace Graphics::Systems::ComponentMigration

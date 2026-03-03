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
    // 1. MeshRenderer → Surface
    // -----------------------------------------------------------------------
    // Every entity with MeshRenderer::Component gets a mirrored
    // Surface::Component. Fields are copied each frame (cheap — no
    // allocation, just a few scalars).
    {
        auto view = registry.view<ECS::MeshRenderer::Component>();
        for (auto entity : view)
        {
            const auto& mr = view.get<ECS::MeshRenderer::Component>(entity);
            auto& surf = registry.get_or_emplace<ECS::Surface::Component>(entity);
            surf.Geometry                          = mr.Geometry;
            surf.Material                          = mr.Material;
            surf.GpuSlot                           = mr.GpuSlot;
            surf.CachedMaterialHandle              = mr.CachedMaterialHandle;
            surf.CachedMaterialHandleForInstance    = mr.CachedMaterialHandleForInstance;
            surf.CachedMaterialRevisionForInstance  = mr.CachedMaterialRevisionForInstance;
            surf.CachedIsSelectedForInstance        = mr.CachedIsSelectedForInstance;
        }
    }

    // -----------------------------------------------------------------------
    // 2. RenderVisualization → Line / Point (attach/detach by toggle)
    // -----------------------------------------------------------------------
    {
        auto view = registry.view<ECS::RenderVisualization::Component>();
        for (auto entity : view)
        {
            const auto& vis = view.get<ECS::RenderVisualization::Component>(entity);

            // --- Wireframe → Line ---
            if (vis.ShowWireframe)
            {
                auto& line = registry.get_or_emplace<ECS::Line::Component>(entity);
                // Mirror wireframe settings.
                line.Color   = vis.WireframeColor;
                line.Width   = vis.WireframeWidth;
                line.Overlay = vis.WireframeOverlay;
                line.HasPerEdgeColors = !vis.CachedEdgeColors.empty();

                // If MeshEdgeView exists, use its geometry handle and edge count.
                if (const auto* ev = registry.try_get<ECS::MeshEdgeView::Component>(entity))
                {
                    line.EdgeView  = ev->Geometry;
                    line.EdgeCount = ev->EdgeCount;
                }
                else
                {
                    line.EdgeView  = {};
                    line.EdgeCount = static_cast<uint32_t>(vis.CachedEdges.size());
                }

                // If MeshRenderer exists, use its geometry handle for shared vertex buffer.
                if (const auto* mr = registry.try_get<ECS::MeshRenderer::Component>(entity))
                {
                    line.Geometry = mr->Geometry;
                }
            }
            else
            {
                // ShowWireframe=false → remove Line component (if not from another source).
                // Only remove if this entity doesn't have Graph::Data (which also provides lines).
                if (!registry.all_of<ECS::Graph::Data>(entity))
                {
                    registry.remove<ECS::Line::Component>(entity);
                }
            }

            // --- Vertices → Point ---
            if (vis.ShowVertices)
            {
                auto& pt = registry.get_or_emplace<ECS::Point::Component>(entity);
                pt.Color = vis.VertexColor;
                pt.Size  = vis.VertexSize;
                pt.Mode  = vis.VertexRenderMode;

                // If MeshVertexView exists, use its geometry handle.
                if (const auto* vv = registry.try_get<ECS::MeshVertexView::Component>(entity))
                {
                    pt.Geometry = vv->Geometry;
                }
                else if (const auto* mr = registry.try_get<ECS::MeshRenderer::Component>(entity))
                {
                    pt.Geometry = mr->Geometry;
                }
            }
            else
            {
                // ShowVertices=false → remove Point component (if not from another source).
                if (!registry.all_of<ECS::Graph::Data>(entity) &&
                    !registry.all_of<ECS::PointCloudRenderer::Component>(entity) &&
                    !registry.all_of<ECS::PointCloud::Data>(entity))
                {
                    registry.remove<ECS::Point::Component>(entity);
                }
            }
        }
    }

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

            // Edges → Line
            {
                auto& line = registry.get_or_emplace<ECS::Line::Component>(entity);
                line.Geometry         = gd.GpuGeometry;
                line.EdgeView         = {};  // Graphs use CachedEdgePairs via LinePass internal buffers
                line.EdgeCount        = static_cast<uint32_t>(gd.CachedEdgePairs.size());
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
            builder.WaitFor("MeshViewLifecycle"_id);
            builder.Signal("ComponentMigration"_id);
        },
        [&registry]()
        {
            OnUpdate(registry);
        });
}

} // namespace Graphics::Systems::ComponentMigration

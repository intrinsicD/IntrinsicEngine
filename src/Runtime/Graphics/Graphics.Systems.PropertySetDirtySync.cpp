module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.PropertySetDirtySync.Impl;

import :Systems.PropertySetDirtySync;
import :Components;
import :GpuColor;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import Geometry;

using namespace Core::Hash;

namespace Graphics::Systems::PropertySetDirtySync
{
    // =====================================================================
    // Graph entity: re-extract per-node attributes from PropertySets.
    // =====================================================================
    // Only safe when the GPU vertex buffer is still valid and the graph
    // vertex count matches GpuVertexCount (no structural changes since
    // last full upload). If counts diverge, escalate to full re-upload.
    static void SyncGraphVertexAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::VertexAttributes, ECS::Graph::Data>();

        for (auto [entity, graphData] : view.each())
        {
            if (!graphData.GraphRef || !graphData.GpuGeometry.IsValid())
            {
                // No graph or no GPU geometry yet — escalate to full re-upload.
                graphData.GpuDirty = true;
                continue;
            }

            auto& graph = *graphData.GraphRef;

            // Safety check: if vertex count diverged (vertices added/deleted
            // since last full upload), attribute-only extraction would produce
            // misaligned data. Escalate to full re-upload.
            if (graphData.GpuVertexCount != graph.VertexCount())
            {
                graphData.GpuDirty = true;
                continue;
            }

            // --- Re-extract per-node colors ---
            graphData.CachedNodeColors.clear();
            if (graph.VertexProperties().Exists("v:color"))
            {
                auto colorProp = Geometry::VertexProperty<glm::vec4>(
                    graph.VertexProperties().Get<glm::vec4>("v:color"));

                graphData.CachedNodeColors.reserve(graphData.GpuVertexCount);

                const std::size_t vSize = graph.VerticesSize();
                for (std::size_t i = 0; i < vSize; ++i)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(v))
                        continue;

                    const glm::vec4& c = colorProp[v];
                    graphData.CachedNodeColors.push_back(
                        GpuColor::PackColorF(c.r, c.g, c.b, c.a));
                }
            }

            // --- Re-extract per-node radii ---
            graphData.CachedNodeRadii.clear();
            if (graph.VertexProperties().Exists("v:radius"))
            {
                auto radiusProp = Geometry::VertexProperty<float>(
                    graph.VertexProperties().Get<float>("v:radius"));

                graphData.CachedNodeRadii.reserve(graphData.GpuVertexCount);

                const std::size_t vSize = graph.VerticesSize();
                for (std::size_t i = 0; i < vSize; ++i)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(v))
                        continue;

                    graphData.CachedNodeRadii.push_back(radiusProp[v]);
                }
            }

            // --- Update Point::Component flags ---
            if (auto* pt = registry.try_get<ECS::Point::Component>(entity))
            {
                pt->HasPerPointColors = !graphData.CachedNodeColors.empty();
                pt->HasPerPointRadii  = !graphData.CachedNodeRadii.empty();
            }
        }
    }

    // =====================================================================
    // Graph entity: re-extract per-edge attributes from PropertySets.
    // =====================================================================
    static void SyncGraphEdgeAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::EdgeAttributes, ECS::Graph::Data>();

        for (auto [entity, graphData] : view.each())
        {
            if (!graphData.GraphRef || !graphData.GpuGeometry.IsValid())
            {
                graphData.GpuDirty = true;
                continue;
            }

            auto& graph = *graphData.GraphRef;

            // Safety: if edge count diverged, escalate.
            if (graphData.GpuEdgeCount != graph.EdgeCount())
            {
                graphData.GpuDirty = true;
                continue;
            }

            // --- Re-extract per-edge colors ---
            graphData.CachedEdgeColors.clear();
            if (graph.EdgeProperties().Exists("e:color"))
            {
                auto colorProp = Geometry::EdgeProperty<glm::vec4>(
                    graph.EdgeProperties().Get<glm::vec4>("e:color"));

                graphData.CachedEdgeColors.reserve(graphData.GpuEdgeCount);

                const std::size_t eSize = graph.EdgesSize();
                for (std::size_t i = 0; i < eSize; ++i)
                {
                    const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(e))
                        continue;

                    const glm::vec4& c = colorProp[e];
                    graphData.CachedEdgeColors.push_back(
                        GpuColor::PackColorF(c.r, c.g, c.b, c.a));
                }
            }

            // --- Update Line::Component flags ---
            if (auto* line = registry.try_get<ECS::Line::Component>(entity))
            {
                line->HasPerEdgeColors = !graphData.CachedEdgeColors.empty();
            }
        }
    }

    // =====================================================================
    // PointCloud entity: re-extract per-point attributes from Cloud.
    // =====================================================================
    static void SyncPointCloudAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::VertexAttributes, ECS::PointCloud::Data>();

        for (auto [entity, pcData] : view.each())
        {
            if (!pcData.CloudRef || !pcData.GpuGeometry.IsValid())
            {
                pcData.GpuDirty = true;
                continue;
            }

            auto& cloud = *pcData.CloudRef;

            // Safety: if point count diverged, escalate.
            if (pcData.GpuPointCount != static_cast<uint32_t>(cloud.Size()))
            {
                pcData.GpuDirty = true;
                continue;
            }

            // --- Re-extract per-point colors ---
            pcData.CachedColors.clear();
            if (cloud.HasColors())
            {
                const auto colors = cloud.Colors();
                pcData.CachedColors.reserve(colors.size());
                for (const auto& c : colors)
                {
                    pcData.CachedColors.push_back(
                        GpuColor::PackColorF(c.r, c.g, c.b, c.a));
                }
            }

            // --- Re-extract per-point radii ---
            pcData.CachedRadii.clear();
            if (cloud.HasRadii())
            {
                const auto radii = cloud.Radii();
                pcData.CachedRadii.assign(radii.begin(), radii.end());
            }

            // --- Update Point::Component flags ---
            if (auto* pt = registry.try_get<ECS::Point::Component>(entity))
            {
                pt->HasPerPointColors = pcData.HasColors();
                pt->HasPerPointRadii  = pcData.HasRadii();
            }
        }
    }

    // =====================================================================
    // Position-dirty: escalate to full re-upload via GpuDirty.
    // =====================================================================
    static void SyncPositionsDirty(entt::registry& registry)
    {
        // Graph entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::Graph::Data>();
            for (auto [entity, graphData] : view.each())
            {
                graphData.GpuDirty = true;
            }
        }

        // PointCloud entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::PointCloud::Data>();
            for (auto [entity, pcData] : view.each())
            {
                pcData.GpuDirty = true;
            }
        }
    }

    // =====================================================================
    // Edge topology dirty: escalate to full re-upload.
    // =====================================================================
    static void SyncEdgeTopologyDirty(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::EdgeTopology, ECS::Graph::Data>();
        for (auto [entity, graphData] : view.each())
        {
            graphData.GpuDirty = true;
        }
    }

    // =====================================================================
    // Face topology dirty: escalate to full re-upload.
    // =====================================================================
    static void SyncFaceTopologyDirty(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::FaceTopology, ECS::Surface::Component>();
        for (auto [entity, surfComp] : view.each())
        {
            surfComp.FaceColorsDirty = true;
        }
    }

    // =====================================================================
    // Face attributes dirty: signal face color SSBO re-upload.
    // =====================================================================
    static void SyncFaceAttributesDirty(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::FaceAttributes, ECS::Surface::Component>();
        for (auto [entity, surfComp] : view.each())
        {
            surfComp.FaceColorsDirty = true;
        }
    }

    // =====================================================================
    // Main entry point.
    // =====================================================================
    void OnUpdate(entt::registry& registry)
    {
        // Process each domain independently. Order matters:
        // 1. Position/topology dirty → escalate to GpuDirty (full re-upload).
        //    Must run first so attribute-only tags on the same entity
        //    don't do redundant work (the full re-upload will re-extract
        //    attributes anyway).
        SyncPositionsDirty(registry);
        SyncEdgeTopologyDirty(registry);
        SyncFaceTopologyDirty(registry);

        // 2. Attribute-only dirty → incremental re-extraction.
        //    These skip entities that were already escalated to GpuDirty
        //    by checking GpuGeometry validity and count consistency.
        SyncGraphVertexAttributes(registry);
        SyncGraphEdgeAttributes(registry);
        SyncPointCloudAttributes(registry);
        SyncFaceAttributesDirty(registry);

        // 3. Clear all dirty tags. Bulk clear is efficient with EnTT.
        registry.clear<ECS::DirtyTag::VertexPositions>();
        registry.clear<ECS::DirtyTag::VertexAttributes>();
        registry.clear<ECS::DirtyTag::EdgeTopology>();
        registry.clear<ECS::DirtyTag::EdgeAttributes>();
        registry.clear<ECS::DirtyTag::FaceTopology>();
        registry.clear<ECS::DirtyTag::FaceAttributes>();
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry)
    {
        graph.AddPass("PropertySetDirtySync",
            [](Core::FrameGraphBuilder& builder)
            {
                // Writes to data components (GpuDirty, cached attributes).
                builder.Write<ECS::Graph::Data>();
                builder.Write<ECS::PointCloud::Data>();
                builder.Write<ECS::Surface::Component>();
                builder.Write<ECS::Line::Component>();
                builder.Write<ECS::Point::Component>();

                // Must run before lifecycle systems that consume GpuDirty.
                builder.Signal("PropertySetDirtySync"_id);
            },
            [&registry]()
            {
                OnUpdate(registry);
            });
    }
}

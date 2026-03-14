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
import :ColorMapper;
import :VisualizationConfig;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import Geometry;

#include "Graphics.GraphPropertyHelpers.hpp"

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

            // --- Re-extract per-node colors and radii via shared helpers ---
            graphData.CachedNodeColors = GraphPropertyHelpers::ExtractNodeColors(
                graph, graphData.Visualization.VertexColors);
            graphData.CachedNodeRadii = GraphPropertyHelpers::ExtractNodeRadii(graph);

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

            // --- Re-extract per-edge colors via shared helper ---
            graphData.CachedEdgeColors = GraphPropertyHelpers::ExtractEdgeColors(
                graph, graphData.Visualization.EdgeColors);

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
            {
                auto& vtxConfig = pcData.Visualization.VertexColors;
                if (vtxConfig.PropertyName.empty() && cloud.HasColors())
                    vtxConfig.PropertyName = "p:color";

                if (auto mapped = ColorMapper::MapProperty(
                        cloud.PointProperties(), vtxConfig))
                {
                    pcData.CachedColors = std::move(mapped->Colors);
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
                pt->HasPerPointColors = !pcData.CachedColors.empty();
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
    // Mesh vertex attributes dirty: extract per-vertex colors from Mesh::Data
    // PropertySets into Surface::Component::CachedVertexColors.
    // =====================================================================
    static void SyncMeshVertexAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::VertexAttributes, ECS::Mesh::Data, ECS::Surface::Component>();
        for (auto [entity, meshData, surfComp] : view.each())
        {
            if (!meshData.MeshRef)
                continue;

            auto& vtxConfig = meshData.Visualization.VertexColors;
            surfComp.CachedVertexColors.clear();

            if (!vtxConfig.PropertyName.empty())
            {
                auto result = ColorMapper::MapProperty(
                    meshData.MeshRef->VertexProperties(), vtxConfig);
                if (result)
                    surfComp.CachedVertexColors = std::move(result->Colors);
            }

            surfComp.VertexColorsDirty = true;
        }
    }

    // =====================================================================
    // Mesh face attributes dirty: extract per-face colors from Mesh::Data
    // PropertySets into Surface::Component::CachedFaceColors.
    // =====================================================================
    static void SyncMeshFaceAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::FaceAttributes, ECS::Mesh::Data, ECS::Surface::Component>();
        for (auto [entity, meshData, surfComp] : view.each())
        {
            if (!meshData.MeshRef)
                continue;

            auto& faceConfig = meshData.Visualization.FaceColors;
            surfComp.CachedFaceColors.clear();

            if (!faceConfig.PropertyName.empty())
            {
                auto result = ColorMapper::MapProperty(
                    meshData.MeshRef->FaceProperties(), faceConfig);
                if (result)
                    surfComp.CachedFaceColors = std::move(result->Colors);
            }

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
        SyncMeshVertexAttributes(registry);
        SyncMeshFaceAttributes(registry);
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

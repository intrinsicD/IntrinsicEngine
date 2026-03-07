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
import :VectorFieldManager;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import Geometry;
import ECS;

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

            // --- Re-extract per-node colors via ColorMapper ---
            graphData.CachedNodeColors.clear();
            {
                auto& vtxConfig = graphData.Visualization.VertexColors;
                if (vtxConfig.PropertyName.empty() && graph.VertexProperties().Exists("v:color"))
                    vtxConfig.PropertyName = "v:color";

                auto skipDeleted = [&graph](size_t i) -> bool {
                    return graph.IsDeleted(
                        Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)});
                };
                if (auto mapped = ColorMapper::MapProperty(
                        graph.VertexProperties(), vtxConfig, skipDeleted))
                {
                    graphData.CachedNodeColors = std::move(mapped->Colors);
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

            // --- Re-extract per-edge colors via ColorMapper ---
            graphData.CachedEdgeColors.clear();
            {
                auto& edgeConfig = graphData.Visualization.EdgeColors;
                if (edgeConfig.PropertyName.empty() && graph.EdgeProperties().Exists("e:color"))
                    edgeConfig.PropertyName = "e:color";

                auto skipDeleted = [&graph](size_t i) -> bool {
                    return graph.IsDeleted(
                        Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(i)});
                };
                if (auto mapped = ColorMapper::MapProperty(
                        graph.EdgeProperties(), edgeConfig, skipDeleted))
                {
                    graphData.CachedEdgeColors = std::move(mapped->Colors);
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
    // Helper: sync vector fields for an entity if it has any configured.
    // Rebuilds child Graph entities with updated base positions.
    // =====================================================================
    static void SyncVectorFieldsForEntity(entt::registry& registry, entt::entity entity)
    {
        std::string entityName;
        if (auto* name = registry.try_get<ECS::Components::NameTag::Component>(entity))
            entityName = name->Name;

        if (auto* md = registry.try_get<ECS::Mesh::Data>(entity))
        {
            if (md->MeshRef && !md->Visualization.VectorFields.empty())
            {
                VectorFieldManager::SyncVectorFields(
                    registry, entity, md->MeshRef->Positions(),
                    md->MeshRef->VertexProperties(), md->Visualization, entityName);
            }
            return;
        }

        if (auto* gd = registry.try_get<ECS::Graph::Data>(entity))
        {
            if (gd->GraphRef && !gd->Visualization.VectorFields.empty())
            {
                auto& graph = *gd->GraphRef;
                std::vector<glm::vec3> positions;
                positions.reserve(graph.VertexCount());
                const std::size_t vSize = graph.VerticesSize();
                for (std::size_t i = 0; i < vSize; ++i)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (!graph.IsDeleted(v))
                        positions.push_back(graph.VertexPosition(v));
                }
                VectorFieldManager::SyncVectorFields(
                    registry, entity, positions, graph.VertexProperties(),
                    gd->Visualization, entityName);
            }
            return;
        }

        if (auto* pcd = registry.try_get<ECS::PointCloud::Data>(entity))
        {
            if (pcd->CloudRef && !pcd->Visualization.VectorFields.empty())
            {
                VectorFieldManager::SyncVectorFields(
                    registry, entity, pcd->CloudRef->Positions(),
                    pcd->CloudRef->PointProperties(), pcd->Visualization, entityName);
            }
            return;
        }
    }

    // =====================================================================
    // Position-dirty: escalate to full re-upload via GpuDirty.
    // Also re-sync vector fields whose arrows depend on base positions.
    // =====================================================================
    static void SyncPositionsDirty(entt::registry& registry)
    {
        // Graph entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::Graph::Data>();
            for (auto [entity, graphData] : view.each())
            {
                graphData.GpuDirty = true;
                SyncVectorFieldsForEntity(registry, entity);
            }
        }

        // PointCloud entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::PointCloud::Data>();
            for (auto [entity, pcData] : view.each())
            {
                pcData.GpuDirty = true;
                SyncVectorFieldsForEntity(registry, entity);
            }
        }

        // Mesh entities (positions dirty → re-sync vector fields)
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::Mesh::Data>();
            for (auto [entity, meshData] : view.each())
            {
                SyncVectorFieldsForEntity(registry, entity);
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
    // Mesh edge attributes dirty: extract per-edge colors from Mesh::Data
    // PropertySets into Line::Component::CachedEdgeColors.
    // =====================================================================
    static void SyncMeshEdgeAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::EdgeAttributes, ECS::Mesh::Data, ECS::Line::Component>();
        for (auto [entity, meshData, lineComp] : view.each())
        {
            if (!meshData.MeshRef)
                continue;

            auto& edgeConfig = meshData.Visualization.EdgeColors;
            lineComp.CachedEdgeColors.clear();

            if (!edgeConfig.PropertyName.empty())
            {
                auto result = ColorMapper::MapProperty(
                    meshData.MeshRef->EdgeProperties(), edgeConfig);
                if (result)
                    lineComp.CachedEdgeColors = std::move(result->Colors);
            }

            lineComp.HasPerEdgeColors = !lineComp.CachedEdgeColors.empty();
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
        SyncMeshEdgeAttributes(registry);
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

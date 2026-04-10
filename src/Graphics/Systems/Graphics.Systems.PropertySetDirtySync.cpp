module;

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics.Systems.PropertySetDirtySync;

import Graphics.Components;
import Graphics.GpuColor;
import Graphics.ColorMapper;
import Graphics.VisualizationConfig;
import Graphics.VectorFieldManager;

import Geometry.Properties;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud;

import ECS;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import Core.SystemFeatureCatalog;

#include "Graphics.GraphPropertyHelpers.hpp"
#include "Graphics.PointCloudPropertyHelpers.hpp"

using namespace Core::Hash;

namespace Graphics::Systems::PropertySetDirtySync
{
    namespace
    {
        [[nodiscard]] std::vector<glm::vec3> BuildMeshEdgeMidpoints(const Geometry::Halfedge::Mesh& mesh)
        {
            std::vector<glm::vec3> positions;
            positions.reserve(mesh.EdgesSize());

            for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
            {
                const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(e) || mesh.IsDeleted(e))
                {
                    positions.emplace_back(0.0f);
                    continue;
                }

                const auto h0 = mesh.Halfedge(e, 0);
                if (!h0.IsValid())
                {
                    positions.emplace_back(0.0f);
                    continue;
                }

                const glm::vec3 a = mesh.Position(mesh.FromVertex(h0));
                const glm::vec3 b = mesh.Position(mesh.ToVertex(h0));
                positions.push_back(0.5f * (a + b));
            }

            return positions;
        }

        [[nodiscard]] std::vector<glm::vec3> BuildMeshFaceCentroids(const Geometry::Halfedge::Mesh& mesh)
        {
            std::vector<glm::vec3> positions;
            positions.reserve(mesh.FacesSize());

            for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
            {
                const Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
                if (!mesh.IsValid(f) || mesh.IsDeleted(f))
                {
                    positions.emplace_back(0.0f);
                    continue;
                }

                const auto h0 = mesh.Halfedge(f);
                if (!h0.IsValid())
                {
                    positions.emplace_back(0.0f);
                    continue;
                }

                glm::vec3 sum(0.0f);
                std::size_t count = 0;
                std::size_t safety = 0;
                const std::size_t maxIter = mesh.HalfedgesSize();
                for (const auto h : mesh.HalfedgesAroundFace(f))
                {
                    sum += mesh.Position(mesh.ToVertex(h));
                    ++count;
                    if (++safety > maxIter)
                        break;
                }

                positions.push_back(count > 0 ? sum / static_cast<float>(count) : mesh.Position(mesh.ToVertex(h0)));
            }

            return positions;
        }

        [[nodiscard]] std::vector<glm::vec3> BuildGraphEdgeMidpoints(const Geometry::Graph::Graph& graph)
        {
            std::vector<glm::vec3> positions;
            positions.reserve(graph.EdgesSize());

            for (std::size_t i = 0; i < graph.EdgesSize(); ++i)
            {
                const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
                if (!graph.IsValid(e) || graph.IsDeleted(e))
                {
                    positions.emplace_back(0.0f);
                    continue;
                }

                const auto [v0, v1] = graph.EdgeVertices(e);
                positions.push_back(0.5f * (graph.VertexPosition(v0) + graph.VertexPosition(v1)));
            }

            return positions;
        }

        template <class PropertySetT>
        void PublishVec3Property(PropertySetT& ps, std::string_view name, std::span<const glm::vec3> values)
        {
            auto property = ps.template GetOrAdd<glm::vec3>(std::string{name}, glm::vec3(0.0f));
            auto& dest = property.Vector();
            dest.resize(values.size());
            const std::size_t count = std::min(dest.size(), values.size());
            for (std::size_t i = 0; i < count; ++i)
                dest[i] = values[i];
        }
    }

    // =====================================================================
    // Graph entity: re-extract per-node attributes from GeometrySources.
    // =====================================================================
    static void SyncGraphVertexAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::VertexAttributes, ECS::Graph::Data>();

        for (auto [entity, graphData] : view.each())
        {
            auto* geoNodes = registry.try_get<ECS::Components::GeometrySources::Nodes>(entity);

            if (!geoNodes || !graphData.GpuGeometry.IsValid())
            {
                // No authoritative data or no GPU geometry yet — escalate.
                graphData.GpuDirty = true;
                continue;
            }

            // Safety: count divergence → escalate to full re-upload.
            const std::size_t aliveNodes =
                ECS::Components::GeometrySources::NodeCount(*geoNodes);
            if (graphData.GpuVertexCount != static_cast<uint32_t>(aliveNodes))
            {
                graphData.GpuDirty = true;
                continue;
            }

            graphData.CachedNodeColors = GraphPropertyHelpers::ExtractNodeColorsFromPropertySet(
                geoNodes->Properties, graphData.Visualization.VertexColors);
            graphData.CachedNodeRadii = GraphPropertyHelpers::ExtractNodeRadiiFromPropertySet(
                geoNodes->Properties);

            if (auto* pt = registry.try_get<ECS::Point::Component>(entity))
            {
                pt->HasPerPointColors = !graphData.CachedNodeColors.empty();
                pt->HasPerPointRadii  = !graphData.CachedNodeRadii.empty();
            }
        }
    }

    // =====================================================================
    // Graph entity: re-extract per-edge attributes from GeometrySources.
    // =====================================================================
    static void SyncGraphEdgeAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::EdgeAttributes, ECS::Graph::Data>();

        for (auto [entity, graphData] : view.each())
        {
            auto* geoEdges = registry.try_get<ECS::Components::GeometrySources::Edges>(entity);

            if (!geoEdges || !graphData.GpuGeometry.IsValid())
            {
                graphData.GpuDirty = true;
                continue;
            }

            const std::size_t aliveEdges =
                ECS::Components::GeometrySources::EdgeCount(*geoEdges);
            if (graphData.GpuEdgeCount != static_cast<uint32_t>(aliveEdges))
            {
                graphData.GpuDirty = true;
                continue;
            }

            graphData.CachedEdgeColors = GraphPropertyHelpers::ExtractEdgeColorsFromPropertySet(
                geoEdges->Properties, graphData.Visualization.EdgeColors);

            if (auto* line = registry.try_get<ECS::Line::Component>(entity))
            {
                line->HasPerEdgeColors = !graphData.CachedEdgeColors.empty();
            }
        }
    }

    // =====================================================================
    // PointCloud entity: re-extract per-point attributes from GeometrySources.
    // =====================================================================
    static void SyncPointCloudAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::VertexAttributes, ECS::PointCloud::Data>();

        for (auto [entity, pcData] : view.each())
        {
            auto* geoVerts = registry.try_get<ECS::Components::GeometrySources::Vertices>(entity);

            if (!geoVerts || !pcData.GpuGeometry.IsValid())
            {
                pcData.GpuDirty = true;
                continue;
            }

            const std::size_t aliveVerts =
                ECS::Components::GeometrySources::VertexCount(*geoVerts);
            if (pcData.GpuPointCount != static_cast<uint32_t>(aliveVerts))
            {
                pcData.GpuDirty = true;
                continue;
            }

            pcData.CachedColors = PointCloudPropertyHelpers::ExtractPointColorsFromPropertySet(
                geoVerts->Properties, pcData.Visualization.VertexColors);
            pcData.CachedRadii = PointCloudPropertyHelpers::ExtractPointRadiiFromPropertySet(
                geoVerts->Properties);

            if (auto* pt = registry.try_get<ECS::Point::Component>(entity))
            {
                pt->HasPerPointColors = !pcData.CachedColors.empty();
                pt->HasPerPointRadii  = !pcData.CachedRadii.empty();
            }
        }
    }

    // =====================================================================
    // Position-dirty: escalate to full re-upload via GpuDirty.
    // =====================================================================
    static void SyncPositionsDirty(entt::registry& registry)
    {
        // Mesh entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::Mesh::Data>();
            for (auto [entity, meshData] : view.each())
            {
                if (!meshData.Visualization.VectorFields.empty())
                    meshData.Visualization.VectorFieldsDirty = true;
            }
        }

        // Graph entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::Graph::Data>();
            for (auto [entity, graphData] : view.each())
            {
                graphData.GpuDirty = true;
                if (!graphData.Visualization.VectorFields.empty())
                    graphData.Visualization.VectorFieldsDirty = true;
            }
        }

        // PointCloud entities
        {
            auto view = registry.view<ECS::DirtyTag::VertexPositions, ECS::PointCloud::Data>();
            for (auto [entity, pcData] : view.each())
            {
                ++pcData.PositionRevision;
                pcData.GpuDirty = true;
                if (!pcData.Visualization.VectorFields.empty())
                    pcData.Visualization.VectorFieldsDirty = true;
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
    // Mesh vertex attributes dirty: extract per-vertex colors.
    // Prefers GeometrySources::Vertices (authoritative) over MeshRef.
    // =====================================================================
    static void SyncMeshVertexAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::VertexAttributes, ECS::Mesh::Data, ECS::Surface::Component>();
        for (auto [entity, meshData, surfComp] : view.each())
        {
            // Resolve the authoritative vertex PropertySet.
            Geometry::PropertySet* vertProps = nullptr;
            if (auto* geoVerts = registry.try_get<ECS::Components::GeometrySources::Vertices>(entity))
                vertProps = &geoVerts->Properties;
            else if (meshData.MeshRef)
                vertProps = &meshData.MeshRef->VertexProperties();

            if (!vertProps)
                continue;

            auto& vtxConfig = meshData.Visualization.VertexColors;
            surfComp.CachedVertexColors.clear();

            if (!vtxConfig.PropertyName.empty())
            {
                auto result = ColorMapper::MapProperty(*vertProps, vtxConfig);
                if (result)
                    surfComp.CachedVertexColors = std::move(result->Colors);
            }

            surfComp.UseNearestVertexColors = meshData.Visualization.UseNearestVertexColors;

            // Centroid-based Voronoi: extract vertex labels + centroid data.
            surfComp.CachedVertexLabels.clear();
            surfComp.CachedCentroids.clear();

            if (surfComp.UseNearestVertexColors && !meshData.KMeansCentroids.empty())
            {
                auto labelProp = vertProps->Get<uint32_t>("v:kmeans_label");
                if (labelProp.IsValid())
                {
                    const auto& labelData = labelProp.Vector();
                    surfComp.CachedVertexLabels.reserve(labelData.size());
                    for (std::size_t i = 0; i < labelData.size(); ++i)
                        surfComp.CachedVertexLabels.push_back(labelData[i]);

                    const uint32_t k = static_cast<uint32_t>(meshData.KMeansCentroids.size());
                    surfComp.CachedCentroids.resize(k);
                    std::vector<bool> colorFound(k, false);

                    const bool haveColors = surfComp.CachedVertexLabels.size() == surfComp.CachedVertexColors.size();
                    for (std::size_t i = 0; i < surfComp.CachedVertexLabels.size() && haveColors; ++i)
                    {
                        const uint32_t label = surfComp.CachedVertexLabels[i];
                        if (label < k && !colorFound[label])
                        {
                            surfComp.CachedCentroids[label] = {meshData.KMeansCentroids[label],
                                                                surfComp.CachedVertexColors[i]};
                            colorFound[label] = true;
                        }
                    }

                    for (uint32_t i = 0; i < k; ++i)
                    {
                        if (!colorFound[i])
                            surfComp.CachedCentroids[i] = {meshData.KMeansCentroids[i],
                                                            GpuColor::PackColorF(1.0f, 1.0f, 1.0f)};
                    }
                }
            }

            surfComp.VertexColorsDirty = true;
        }
    }

    // =====================================================================
    // Mesh face attributes dirty: extract per-face colors.
    // Prefers GeometrySources::Faces (authoritative) over MeshRef.
    // =====================================================================
    static void SyncMeshFaceAttributes(entt::registry& registry)
    {
        auto view = registry.view<ECS::DirtyTag::FaceAttributes, ECS::Mesh::Data, ECS::Surface::Component>();
        for (auto [entity, meshData, surfComp] : view.each())
        {
            Geometry::PropertySet* faceProps = nullptr;
            if (auto* geoFaces = registry.try_get<ECS::Components::GeometrySources::Faces>(entity))
                faceProps = &geoFaces->Properties;
            else if (meshData.MeshRef)
                faceProps = &meshData.MeshRef->FaceProperties();

            if (!faceProps)
                continue;

            auto& faceConfig = meshData.Visualization.FaceColors;
            surfComp.CachedFaceColors.clear();

            if (!faceConfig.PropertyName.empty())
            {
                auto result = ColorMapper::MapProperty(*faceProps, faceConfig);
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
    // Vector field sync: create/update/destroy child Graph entities for
    // each entity type that has VectorFieldsDirty or position changes.
    // =====================================================================
    static void SyncVectorFields(entt::registry& registry)
    {
        // Helper to get entity display name.
        auto getName = [&](entt::entity e) -> std::string
        {
            if (auto* name = registry.try_get<ECS::Components::NameTag::Component>(e))
                return name->Name;
            return "Entity";
        };

        // Mesh entities — prefer GeometrySources for positions/properties,
        // fall back to MeshRef for topology-dependent computations (midpoints).
        {
            auto view = registry.view<ECS::Mesh::Data>();
            for (auto [entity, meshData] : view.each())
            {
                auto& viz = meshData.Visualization;
                if (!viz.VectorFieldsDirty)
                    continue;

                // Resolve vertex positions.
                std::span<const glm::vec3> vertPositions;
                Geometry::PropertySet* vertProps = nullptr;
                if (auto* geoVerts = registry.try_get<ECS::Components::GeometrySources::Vertices>(entity))
                {
                    vertProps = &geoVerts->Properties;
                    if (auto posProp = geoVerts->Properties.Get<glm::vec3>("v:position"))
                        vertPositions = posProp.Span();
                }

                // Topology helpers still require MeshRef when GeometrySources
                // halfedge topology is not yet used by VectorFieldManager.
                if (!vertPositions.empty() && vertProps)
                {
                    if (meshData.MeshRef)
                    {
                        PublishVec3Property(
                            meshData.MeshRef->EdgeProperties(),
                            "e:vf_center",
                            BuildMeshEdgeMidpoints(*meshData.MeshRef));
                        // Mirror into GeometrySources::Edges if present.
                        if (auto* geoEdges = registry.try_get<ECS::Components::GeometrySources::Edges>(entity))
                            PublishVec3Property(geoEdges->Properties, "e:vf_center",
                                                BuildMeshEdgeMidpoints(*meshData.MeshRef));

                        PublishVec3Property(
                            meshData.MeshRef->FaceProperties(),
                            "f:vf_center",
                            BuildMeshFaceCentroids(*meshData.MeshRef));
                        if (auto* geoFaces = registry.try_get<ECS::Components::GeometrySources::Faces>(entity))
                            PublishVec3Property(geoFaces->Properties, "f:vf_center",
                                                BuildMeshFaceCentroids(*meshData.MeshRef));

                        VectorFieldManager::SyncVectorFields(
                            registry, entity, vertPositions,
                            Graphics::VectorFieldDomain::Vertex,
                            *vertProps, viz, getName(entity));

                        VectorFieldManager::SyncVectorFields(
                            registry, entity,
                            BuildMeshEdgeMidpoints(*meshData.MeshRef),
                            Graphics::VectorFieldDomain::Edge,
                            meshData.MeshRef->EdgeProperties(),
                            viz, getName(entity));

                        VectorFieldManager::SyncVectorFields(
                            registry, entity,
                            BuildMeshFaceCentroids(*meshData.MeshRef),
                            Graphics::VectorFieldDomain::Face,
                            meshData.MeshRef->FaceProperties(),
                            viz, getName(entity));
                    }
                    else
                    {
                        // No MeshRef — only vertex-domain vector fields are available.
                        VectorFieldManager::SyncVectorFields(
                            registry, entity, vertPositions,
                            Graphics::VectorFieldDomain::Vertex,
                            *vertProps, viz, getName(entity));
                    }
                    viz.VectorFieldsDirty = false;
                }
                else if (meshData.MeshRef)
                {
                    // Legacy path: no GeometrySources, use MeshRef directly.
                    PublishVec3Property(
                        meshData.MeshRef->EdgeProperties(),
                        "e:vf_center",
                        BuildMeshEdgeMidpoints(*meshData.MeshRef));
                    PublishVec3Property(
                        meshData.MeshRef->FaceProperties(),
                        "f:vf_center",
                        BuildMeshFaceCentroids(*meshData.MeshRef));

                    VectorFieldManager::SyncVectorFields(
                        registry, entity, meshData.MeshRef->Positions(),
                        Graphics::VectorFieldDomain::Vertex,
                        meshData.MeshRef->VertexProperties(), viz, getName(entity));
                    VectorFieldManager::SyncVectorFields(
                        registry, entity,
                        BuildMeshEdgeMidpoints(*meshData.MeshRef),
                        Graphics::VectorFieldDomain::Edge,
                        meshData.MeshRef->EdgeProperties(), viz, getName(entity));
                    VectorFieldManager::SyncVectorFields(
                        registry, entity,
                        BuildMeshFaceCentroids(*meshData.MeshRef),
                        Graphics::VectorFieldDomain::Face,
                        meshData.MeshRef->FaceProperties(), viz, getName(entity));
                    viz.VectorFieldsDirty = false;
                }
                else
                {
                    viz.VectorFieldsDirty = false;
                }
            }
        }

        // Graph entities — prefer GeometrySources::Nodes for positions.
        {
            auto view = registry.view<ECS::Graph::Data>();
            for (auto [entity, graphData] : view.each())
            {
                auto& viz = graphData.Visualization;
                if (!viz.VectorFieldsDirty)
                    continue;

                auto* geoNodes = registry.try_get<ECS::Components::GeometrySources::Nodes>(entity);
                auto* geoEdges = registry.try_get<ECS::Components::GeometrySources::Edges>(entity);

                if (geoNodes)
                {
                    auto posProp = geoNodes->Properties.Get<glm::vec3>("v:position");
                    if (!posProp) { viz.VectorFieldsDirty = false; continue; }

                    const auto& posVec = posProp.Vector();
                    const std::size_t vSize = posVec.size();

                    // Build edge midpoints from GeometrySources connectivity.
                    std::vector<glm::vec3> edgeMidpoints;
                    if (geoEdges)
                    {
                        auto v0Prop = geoEdges->Properties.Get<uint32_t>("e:v0");
                        auto v1Prop = geoEdges->Properties.Get<uint32_t>("e:v1");
                        if (v0Prop && v1Prop)
                        {
                            const auto& v0Vec = v0Prop.Vector();
                            const auto& v1Vec = v1Prop.Vector();
                            edgeMidpoints.reserve(std::min(v0Vec.size(), v1Vec.size()));
                            for (std::size_t i = 0; i < std::min(v0Vec.size(), v1Vec.size()); ++i)
                            {
                                const uint32_t i0 = v0Vec[i], i1 = v1Vec[i];
                                if (i0 < vSize && i1 < vSize)
                                    edgeMidpoints.push_back(0.5f * (posVec[i0] + posVec[i1]));
                                else
                                    edgeMidpoints.emplace_back(0.0f);
                            }
                            PublishVec3Property(geoEdges->Properties, "e:vf_center", edgeMidpoints);
                        }
                    }

                    VectorFieldManager::SyncVectorFields(
                        registry, entity, posVec,
                        Graphics::VectorFieldDomain::Vertex,
                        geoNodes->Properties, viz, getName(entity));

                    if (geoEdges && !edgeMidpoints.empty())
                    {
                        VectorFieldManager::SyncVectorFields(
                            registry, entity, edgeMidpoints,
                            Graphics::VectorFieldDomain::Edge,
                            geoEdges->Properties, viz, getName(entity));
                    }
                    viz.VectorFieldsDirty = false;
                }
                else if (graphData.GraphRef)
                {
                    // Legacy path: no GeometrySources, use GraphRef directly.
                    auto& graph = *graphData.GraphRef;
                    const std::size_t vSize = graph.VerticesSize();
                    std::vector<glm::vec3> positions(vSize);
                    for (std::size_t i = 0; i < vSize; ++i)
                    {
                        const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                        positions[i] = graph.IsDeleted(v) ? glm::vec3(0.0f) : graph.VertexPosition(v);
                    }

                    PublishVec3Property(graph.EdgeProperties(), "e:vf_center",
                                        BuildGraphEdgeMidpoints(graph));

                    VectorFieldManager::SyncVectorFields(
                        registry, entity, positions,
                        Graphics::VectorFieldDomain::Vertex,
                        graph.VertexProperties(), viz, getName(entity));
                    VectorFieldManager::SyncVectorFields(
                        registry, entity, BuildGraphEdgeMidpoints(graph),
                        Graphics::VectorFieldDomain::Edge,
                        graph.EdgeProperties(), viz, getName(entity));
                    viz.VectorFieldsDirty = false;
                }
                else
                {
                    viz.VectorFieldsDirty = false;
                }
            }
        }

        // PointCloud entities — prefer GeometrySources::Vertices.
        {
            auto view = registry.view<ECS::PointCloud::Data>();
            for (auto [entity, pcData] : view.each())
            {
                auto& viz = pcData.Visualization;
                if (!viz.VectorFieldsDirty)
                    continue;

                if (auto* geoVerts = registry.try_get<ECS::Components::GeometrySources::Vertices>(entity))
                {
                    auto posProp = geoVerts->Properties.Get<glm::vec3>("v:position");
                    if (!posProp) { viz.VectorFieldsDirty = false; continue; }

                    VectorFieldManager::SyncVectorFields(
                        registry, entity, posProp.Vector(),
                        Graphics::VectorFieldDomain::Vertex,
                        geoVerts->Properties, viz, getName(entity));
                    viz.VectorFieldsDirty = false;
                }
                else if (pcData.CloudRef)
                {
                    VectorFieldManager::SyncVectorFields(
                        registry, entity, pcData.CloudRef->Positions(),
                        Graphics::VectorFieldDomain::Vertex,
                        pcData.CloudRef->PointProperties(), viz, getName(entity));
                    viz.VectorFieldsDirty = false;
                }
                else
                {
                    viz.VectorFieldsDirty = false;
                }
            }
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

        // 3. Vector field sync: create/update/destroy child Graph entities.
        //    Runs after attribute extraction so source properties are fresh.
        SyncVectorFields(registry);

        // 4. Clear all dirty tags. Bulk clear is efficient with EnTT.
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
        graph.AddPass(Runtime::SystemFeatureCatalog::PassNames::PropertySetDirtySync,
            [](Core::FrameGraphBuilder& builder)
            {
                // Writes to data components (GpuDirty, cached attributes,
                // VectorFieldsDirty).
                builder.Write<ECS::Mesh::Data>();
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

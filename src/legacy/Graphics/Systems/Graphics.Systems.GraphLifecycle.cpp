module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Graphics.Systems.GraphLifecycle;

import Graphics.Components;
import Graphics.Geometry;
import Graphics.GPUScene;
import Graphics.GpuColor;
import Graphics.ColorMapper;
import Graphics.VisualizationConfig;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;

import ECS;

import Geometry.Graph;
import Geometry.Properties;
import Geometry.Handle;

import RHI.Device;
import RHI.Transfer;

import Core.SystemFeatureCatalog;
import Graphics.LifecycleUtils;

#include "Graphics.GraphPropertyHelpers.hpp"

using namespace Core::Hash;
using namespace Graphics::LifecycleUtils;

namespace Graphics::Systems::GraphLifecycle
{

    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  RHI::BufferManager& bufferManager,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager,
                  entt::dispatcher& dispatcher)
    {
        auto view = registry.view<ECS::Graph::Data>();

        // Helper: release all GPU geometry and cached data for a graph entity.
        auto releaseGraphGpu = [&](ECS::Graph::Data& gd) {
            if (gd.GpuGeometry.IsValid())
            {
                geometryStorage.Remove(gd.GpuGeometry, device->GetGlobalFrameNumber());
                gd.GpuGeometry = {};
            }
            if (gd.GpuEdgeGeometry.IsValid())
            {
                geometryStorage.Remove(gd.GpuEdgeGeometry, device->GetGlobalFrameNumber());
                gd.GpuEdgeGeometry = {};
                gd.GpuEdgeCount = 0;
            }
            gd.CachedEdgePairs.clear();
            gd.CachedEdgeColors.clear();
            gd.CachedNodeColors.clear();
            gd.CachedNodeRadii.clear();
            gd.GpuVertexCount = 0;
            gd.GpuDirty = false;
            gd.VectorFieldMode = false;
        };

        for (auto [entity, graphData] : view.each())
        {
            // -----------------------------------------------------------------
            // Phase 1: Re-upload geometry when dirty.
            // -----------------------------------------------------------------
            if (graphData.GpuDirty)
            {
                // Sync GeometrySources from GraphRef when it is available.
                // GraphRef is an optional computation tool — after this call,
                // GeometrySources::Nodes / Edges are the authoritative CPU data.
                if (graphData.GraphRef && graphData.GraphRef->EdgeCount() > 0)
                {
                    ECS::Components::GeometrySources::PopulateFromGraph(
                        registry, entity, *graphData.GraphRef);
                }

                // Read from authoritative GeometrySources.
                auto* geoNodes = registry.try_get<ECS::Components::GeometrySources::Nodes>(entity);
                auto* geoEdges = registry.try_get<ECS::Components::GeometrySources::Edges>(entity);

                if (!geoNodes)
                {
                    // No data source — release any existing GPU geometry.
                    releaseGraphGpu(graphData);
                    // Fall through to Phase 3 (GpuGeometry invalid → skip).
                }
                else
                {
                    auto posProp = geoNodes->Properties.Get<glm::vec3>("v:position");

                    if (!posProp || posProp.Vector().empty())
                    {
                        releaseGraphGpu(graphData);
                    }
                    else
                    {
                        const auto& posVec = posProp.Vector();
                        const std::size_t vSize = posVec.size();
                        const bool vectorFieldMode = graphData.VectorFieldMode;

                        std::vector<glm::vec3> positions(posVec.begin(), posVec.end());
                        std::vector<glm::vec3> normals(vSize, glm::vec3(0.0f, 1.0f, 0.0f));

                        // --- Extract per-node colors and radii from GeometrySources ---
                        std::vector<uint32_t> nodeColors =
                            GraphPropertyHelpers::ExtractNodeColorsFromPropertySet(
                                geoNodes->Properties, graphData.Visualization.VertexColors);
                        std::vector<float> nodeRadii =
                            GraphPropertyHelpers::ExtractNodeRadiiFromPropertySet(
                                geoNodes->Properties);

                        // --- Extract edge pairs from GeometrySources::Edges ---
                        std::vector<ECS::EdgePair> edgePairs;
                        std::vector<uint32_t> edgeColors;

                        if (geoEdges)
                        {
                            auto v0Prop = geoEdges->Properties.Get<uint32_t>("e:v0");
                            auto v1Prop = geoEdges->Properties.Get<uint32_t>("e:v1");

                            if (v0Prop && v1Prop)
                            {
                                const auto& v0Vec = v0Prop.Vector();
                                const auto& v1Vec = v1Prop.Vector();
                                const std::size_t eCount = std::min(v0Vec.size(), v1Vec.size());
                                edgePairs.reserve(eCount);

                                for (std::size_t i = 0; i < eCount; ++i)
                                {
                                    const uint32_t ci0 = v0Vec[i];
                                    const uint32_t ci1 = v1Vec[i];

                                    if (!vectorFieldMode)
                                    {
                                        if (ci0 == ci1)
                                            continue;
                                        if (ci0 >= vSize || ci1 >= vSize)
                                            continue;
                                        const glm::vec3 d = positions[ci1] - positions[ci0];
                                        if (glm::dot(d, d) < 1e-12f)
                                            continue;
                                    }
                                    edgePairs.push_back({ci0, ci1});
                                }
                            }

                            edgeColors = GraphPropertyHelpers::ExtractEdgeColorsFromPropertySet(
                                geoEdges->Properties, graphData.Visualization.EdgeColors);
                        }

                        // Release previous geometry before allocating new.
                        if (graphData.GpuGeometry.IsValid())
                        {
                            geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                            graphData.GpuGeometry = {};
                        }

                        // --- Upload to GPU ---
                        const auto uploadMode = graphData.StaticGeometry
                            ? GeometryUploadMode::Staged
                            : GeometryUploadMode::Direct;

                        GeometryUploadRequest vertexUpload{};
                        vertexUpload.Positions = positions;
                        vertexUpload.Normals = normals;
                        vertexUpload.Topology = PrimitiveTopology::Points;
                        vertexUpload.UploadMode = uploadMode;

                        auto [newGpuData, token] = GeometryGpuData::CreateAsync(
                            device, transferManager, bufferManager, vertexUpload, &geometryStorage);

                        if (!newGpuData || !newGpuData->GetVertexBuffer())
                        {
                            HandleUploadFailure(dispatcher, entity, "GraphLifecycle");
                            graphData.CachedEdgePairs.clear();
                            graphData.CachedEdgeColors.clear();
                            graphData.CachedNodeColors.clear();
                            graphData.CachedNodeRadii.clear();
                            graphData.GpuVertexCount = 0;
                            graphData.GpuDirty = false;
                        }
                        else
                        {

                        graphData.GpuGeometry = geometryStorage.Add(std::move(*newGpuData));

                        // --- Create edge index buffer via ReuseVertexBuffersFrom ---
                        if (graphData.GpuEdgeGeometry.IsValid())
                        {
                            geometryStorage.Remove(graphData.GpuEdgeGeometry, device->GetGlobalFrameNumber());
                            graphData.GpuEdgeGeometry = {};
                            graphData.GpuEdgeCount = 0;
                        }

                        if (!edgePairs.empty())
                        {
                            std::vector<uint32_t> edgeIndices;
                            edgeIndices.reserve(edgePairs.size() * 2);
                            for (const auto& [i0, i1] : edgePairs)
                            {
                                edgeIndices.push_back(i0);
                                edgeIndices.push_back(i1);
                            }

                            GeometryUploadRequest edgeReq{};
                            edgeReq.ReuseVertexBuffersFrom = graphData.GpuGeometry;
                            edgeReq.Indices = edgeIndices;
                            edgeReq.Topology = PrimitiveTopology::Lines;
                            edgeReq.UploadMode = uploadMode;

                            auto [edgeGpuData, edgeToken] = GeometryGpuData::CreateAsync(
                                device, transferManager, bufferManager, edgeReq, &geometryStorage);

                            if (edgeGpuData && edgeGpuData->GetIndexBuffer())
                            {
                                graphData.GpuEdgeGeometry = geometryStorage.Add(std::move(*edgeGpuData));
                                graphData.GpuEdgeCount = static_cast<uint32_t>(edgePairs.size());
                            }
                            else
                            {
                                Core::Log::Warn("GraphLifecycle: Failed to create edge index buffer for entity {}",
                                                static_cast<uint32_t>(entity));
                            }
                        }

                        graphData.CachedEdgePairs = std::move(edgePairs);
                        graphData.CachedEdgeColors = std::move(edgeColors);
                        graphData.CachedNodeColors = std::move(nodeColors);
                        graphData.CachedNodeRadii = std::move(nodeRadii);
                        graphData.GpuVertexCount = static_cast<uint32_t>(vSize);
                        graphData.GpuDirty = false;

                        } // else (upload succeeded)
                    } // else (positions not empty)
                } // else (geoNodes present)
            } // if (GpuDirty)

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // Allocate once, then GPUSceneSync handles subsequent transform-only updates.
            // -----------------------------------------------------------------
            graphData.GpuSlot = TryAllocateGpuSlot(
                registry, entity, gpuScene, geometryStorage,
                graphData.GpuSlot, graphData.GpuGeometry);

            // -----------------------------------------------------------------
            // Phase 3: Populate per-pass typed ECS components.
            // -----------------------------------------------------------------
            const bool gpuValid = graphData.GpuGeometry.IsValid();

            // Ensure DataAuthority tag is present (single-authority invariant).
            if (!registry.all_of<ECS::DataAuthority::GraphTag>(entity))
            {
                assert((!registry.any_of<ECS::DataAuthority::MeshTag, ECS::DataAuthority::PointCloudTag>(entity))
                       && "Entity already has a different DataAuthority tag");
                registry.emplace<ECS::DataAuthority::GraphTag>(entity);
            }

            PopulateOrRemovePassComponent<ECS::Line::Component>(
                registry, entity, graphData.Visible, gpuValid,
                [&](ECS::Line::Component& line) {
                    line.Geometry         = graphData.GpuGeometry;
                    line.EdgeView         = graphData.GpuEdgeGeometry;
                    line.EdgeCount        = graphData.GpuEdgeCount;
                    line.Color            = graphData.DefaultEdgeColor;
                    line.Width            = graphData.EdgeWidth;
                    line.Overlay          = graphData.EdgesOverlay;
                    line.HasPerEdgeColors = !graphData.CachedEdgeColors.empty();
                    line.SourceDomain     = ECS::Line::Domain::GraphEdge;
                });

            PopulateOrRemovePassComponent<ECS::Point::Component>(
                registry, entity, graphData.Visible && !graphData.VectorFieldMode, gpuValid,
                [&](ECS::Point::Component& pt) {
                    pt.Geometry          = graphData.GpuGeometry;
                    pt.Color             = graphData.DefaultNodeColor;
                    pt.Size              = graphData.DefaultNodeRadius;
                    pt.SizeMultiplier    = graphData.NodeSizeMultiplier;
                    pt.Mode              = graphData.NodeRenderMode;
                    pt.HasPerPointColors = !graphData.CachedNodeColors.empty();
                    pt.HasPerPointRadii  = !graphData.CachedNodeRadii.empty();
                    pt.SourceDomain      = ECS::Point::Domain::GraphNode;
                });
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        RHI::BufferManager& bufferManager,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager,
                        entt::dispatcher& dispatcher)
    {
        graph.AddPass(Runtime::SystemFeatureCatalog::PassNames::GraphLifecycle,
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Write<ECS::Graph::Data>();
                builder.Write<ECS::Line::Component>();
                builder.Write<ECS::Point::Component>();
                builder.WaitFor("TransformUpdate"_id);
                builder.WaitFor("PropertySetDirtySync"_id);
            },
            [&registry, &gpuScene, &geometryStorage, &bufferManager, device, &transferManager, &dispatcher]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, bufferManager, device, transferManager, dispatcher);
            });
    }
}

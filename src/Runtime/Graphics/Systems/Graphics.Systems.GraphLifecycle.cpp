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
                if (!graphData.GraphRef || graphData.GraphRef->VertexCount() == 0)
                {
                    // Graph is empty or null — release any existing GPU geometry.
                    releaseGraphGpu(graphData);
                    // Fall through to Phase 3 (GpuGeometry invalid → skip).
                }
                else
                {

                auto& graph = *graphData.GraphRef;
                const bool vectorFieldMode = graphData.VectorFieldMode;

                // Run garbage collection if the graph has deleted elements to ensure
                // contiguous storage. This simplifies the compaction step.
                if (graph.HasGarbage())
                    graph.GarbageCollection();

                // --- Extract compacted positions and normals ---
                // After GC, there are no deleted vertices — direct iteration is safe.
                const std::size_t vSize = graph.VerticesSize();
                std::vector<glm::vec3> positions;
                std::vector<glm::vec3> normals;
                positions.reserve(vSize);
                normals.reserve(vSize);

                // Build remap table: original vertex index → compacted index.
                // After GC, this is typically identity, but we handle the general case.
                std::vector<uint32_t> remap(vSize, UINT32_MAX);
                uint32_t compactIdx = 0;

                for (std::size_t i = 0; i < vSize; ++i)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(v))
                        continue;

                    positions.push_back(graph.VertexPosition(v));
                    // Graph nodes have no meaningful surface normal — use world-up default.
                    normals.emplace_back(0.0f, 1.0f, 0.0f);
                    remap[i] = compactIdx++;
                }

                // --- Extract per-node colors and radii via shared helpers ---
                std::vector<uint32_t> nodeColors = GraphPropertyHelpers::ExtractNodeColors(
                    graph, graphData.Visualization.VertexColors);
                std::vector<float> nodeRadii = GraphPropertyHelpers::ExtractNodeRadii(graph);

                if (positions.empty())
                {
                    releaseGraphGpu(graphData);
                    // Fall through to Phase 3 (GpuGeometry invalid → skip).
                }
                else
                {

                // --- Extract edge pairs (remapped to compacted indices) ---
                const std::size_t eSize = graph.EdgesSize();
                std::vector<ECS::EdgePair> edgePairs;
                edgePairs.reserve(eSize);

                for (std::size_t i = 0; i < eSize; ++i)
                {
                    const Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(e))
                        continue;

                    const auto [v0, v1] = graph.EdgeVertices(e);
                    const uint32_t ci0 = remap[v0.Index];
                    const uint32_t ci1 = remap[v1.Index];

                    if (ci0 == UINT32_MAX || ci1 == UINT32_MAX)
                        continue; // Edge references a deleted vertex — skip.

                    // Skip zero-length edges (coincident endpoints) — these would
                    // produce degenerate line quads. Nodes are still rendered as
                    // points via Point::Component.
                    if (!vectorFieldMode)
                    {
                        if (ci0 == ci1)
                            continue;
                        {
                            const glm::vec3& p0 = positions[ci0];
                            const glm::vec3& p1 = positions[ci1];
                            const glm::vec3 d = p1 - p0;
                            if (glm::dot(d, d) < 1e-12f)
                                continue;
                        }
                    }

                    edgePairs.push_back({ci0, ci1});
                }

                // --- Extract per-edge colors via shared helper ---
                std::vector<uint32_t> edgeColors = GraphPropertyHelpers::ExtractEdgeColors(
                    graph, graphData.Visualization.EdgeColors);

                // Release previous geometry before allocating new.
                if (graphData.GpuGeometry.IsValid())
                {
                    geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                    graphData.GpuGeometry = {};
                }

                // --- Upload to GPU ---
                // Upload mode is selected by the StaticGeometry flag on the component:
                //   StaticGeometry = true  → Staged (device-local, GPU_ONLY) — optimal
                //     for graphs that don't change every frame (file-loaded, computed once).
                //   StaticGeometry = false → Direct (host-visible, CPU_TO_GPU) — suitable
                //     for dynamic graphs undergoing frequent re-layout.
                const auto uploadMode = graphData.StaticGeometry
                    ? GeometryUploadMode::Staged
                    : GeometryUploadMode::Direct;

                GeometryUploadRequest vertexUpload{};
                vertexUpload.Positions = positions;
                vertexUpload.Normals = normals;
                vertexUpload.Topology = PrimitiveTopology::Points;
                vertexUpload.UploadMode = uploadMode;

                auto [newGpuData, token] = GeometryGpuData::CreateAsync(
                    device, transferManager, vertexUpload, &geometryStorage);

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

                graphData.GpuGeometry = geometryStorage.Add(std::move(newGpuData));

                // --- Create edge index buffer via ReuseVertexBuffersFrom ---
                // Shares the vertex buffer (positions) with the node geometry.
                // LinePass reads edge pairs from this geometry's index buffer
                // via BDA — no LinePass-internal edge buffer management needed.
                if (graphData.GpuEdgeGeometry.IsValid())
                {
                    geometryStorage.Remove(graphData.GpuEdgeGeometry, device->GetGlobalFrameNumber());
                    graphData.GpuEdgeGeometry = {};
                    graphData.GpuEdgeCount = 0;
                }

                if (!edgePairs.empty())
                {
                    // Flatten EdgePair array to contiguous uint32_t indices.
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
                        device, transferManager, edgeReq, &geometryStorage);

                    if (edgeGpuData && edgeGpuData->GetIndexBuffer())
                    {
                        graphData.GpuEdgeGeometry = geometryStorage.Add(std::move(edgeGpuData));
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
                graphData.GpuVertexCount = compactIdx;
                graphData.GpuDirty = false;

                } // else (upload succeeded)
                } // else (positions not empty)
                } // else (graph not empty)
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
            [&registry, &gpuScene, &geometryStorage, device, &transferManager, &dispatcher]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, device, transferManager, dispatcher);
            });
    }
}

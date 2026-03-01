module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.GraphGeometrySync.Impl;

import :Systems.GraphGeometrySync;
import :Components;
import :Geometry;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import ECS;
import Geometry;
import RHI;

using namespace Core::Hash;

namespace Graphics::Systems::GraphGeometrySync
{

    void OnUpdate(entt::registry& registry,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager)
    {
        auto view = registry.view<ECS::Graph::Data>();

        for (auto [entity, graphData] : view.each())
        {
            if (!graphData.GpuDirty)
                continue;

            if (!graphData.GraphRef || graphData.GraphRef->VertexCount() == 0)
            {
                // Graph is empty or null — release any existing GPU geometry.
                if (graphData.GpuGeometry.IsValid())
                {
                    geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                    graphData.GpuGeometry = {};
                }
                graphData.CachedEdgePairs.clear();
                graphData.GpuVertexCount = 0;
                graphData.GpuDirty = false;
                continue;
            }

            auto& graph = *graphData.GraphRef;

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

            if (positions.empty())
            {
                if (graphData.GpuGeometry.IsValid())
                {
                    geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                    graphData.GpuGeometry = {};
                }
                graphData.CachedEdgePairs.clear();
                graphData.GpuVertexCount = 0;
                graphData.GpuDirty = false;
                continue;
            }

            // --- Extract edge pairs (remapped to compacted indices) ---
            const std::size_t eSize = graph.EdgesSize();
            std::vector<ECS::RenderVisualization::EdgePair> edgePairs;
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

                edgePairs.push_back({ci0, ci1});
            }

            // Release previous geometry before allocating new.
            if (graphData.GpuGeometry.IsValid())
            {
                geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                graphData.GpuGeometry = {};
            }

            // --- Upload to GPU via Direct mode (CPU_TO_GPU for dynamic graphs) ---
            // Direct mode creates host-visible buffers with SHADER_DEVICE_ADDRESS_BIT
            // for BDA access. Suitable for graphs that may re-layout frequently.
            // Edge pairs are NOT uploaded here — they go to the per-frame SSBO in
            // RetainedLineRenderPass, same pattern as mesh wireframe CachedEdges.
            GeometryUploadRequest directUpload{};
            directUpload.Positions = positions;
            directUpload.Normals = normals;
            directUpload.Topology = PrimitiveTopology::Points;
            directUpload.UploadMode = GeometryUploadMode::Direct;

            auto [newGpuData, token] = GeometryGpuData::CreateAsync(
                device, transferManager, directUpload, &geometryStorage);

            if (!newGpuData || !newGpuData->GetVertexBuffer())
            {
                Core::Log::Error("GraphGeometrySync: Failed to create GPU geometry for entity {}",
                                 static_cast<uint32_t>(entity));
                graphData.GpuDirty = false;
                continue;
            }

            graphData.GpuGeometry = geometryStorage.Add(std::move(newGpuData));
            graphData.CachedEdgePairs = std::move(edgePairs);
            graphData.GpuVertexCount = compactIdx;
            graphData.GpuDirty = false;
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager)
    {
        graph.AddPass("GraphGeometrySync",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Write<ECS::Graph::Data>();
                builder.WaitFor("TransformUpdate"_id);
            },
            [&registry, &geometryStorage, device, &transferManager]()
            {
                OnUpdate(registry, geometryStorage, device, transferManager);
            });
    }
}

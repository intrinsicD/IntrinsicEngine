module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.GraphGeometrySync.Impl;

import :Systems.GraphGeometrySync;
import :Components;
import :Geometry;
import :GPUScene;
import :GpuColor;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import ECS;
import Geometry;
import RHI;

using namespace Core::Hash;

namespace Graphics::Systems::GraphGeometrySync
{
    namespace
    {
        [[nodiscard]] auto ComputeLocalBoundingSphere(const GeometryGpuData& geo) -> glm::vec4
        {
            const glm::vec4 bounds = geo.GetLocalBoundingSphere();
            if (bounds.w > 0.0f)
                return bounds;

            return {0.0f, 0.0f, 0.0f, GPUSceneConstants::kDefaultBoundingSphereRadius};
        }
    }

    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager)
    {
        auto view = registry.view<ECS::Graph::Data>();

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
                    if (graphData.GpuGeometry.IsValid())
                    {
                        geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                        graphData.GpuGeometry = {};
                    }
                    if (graphData.GpuEdgeGeometry.IsValid())
                    {
                        geometryStorage.Remove(graphData.GpuEdgeGeometry, device->GetGlobalFrameNumber());
                        graphData.GpuEdgeGeometry = {};
                        graphData.GpuEdgeCount = 0;
                    }
                    graphData.CachedEdgePairs.clear();
                    graphData.CachedEdgeColors.clear();
                    graphData.CachedNodeColors.clear();
                    graphData.CachedNodeRadii.clear();
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

                // Check for per-node color property ("v:color" as glm::vec4).
                const bool hasNodeColors = graph.VertexProperties().Exists("v:color");
                std::optional<Geometry::VertexProperty<glm::vec4>> nodeColorProp;
                if (hasNodeColors)
                    nodeColorProp = Geometry::VertexProperty<glm::vec4>(
                        graph.VertexProperties().Get<glm::vec4>("v:color"));

                // Check for per-node radius property ("v:radius" as float).
                const bool hasNodeRadii = graph.VertexProperties().Exists("v:radius");
                std::optional<Geometry::VertexProperty<float>> nodeRadiusProp;
                if (hasNodeRadii)
                    nodeRadiusProp = Geometry::VertexProperty<float>(
                        graph.VertexProperties().Get<float>("v:radius"));

                std::vector<uint32_t> nodeColors;
                if (hasNodeColors)
                    nodeColors.reserve(vSize);

                std::vector<float> nodeRadii;
                if (hasNodeRadii)
                    nodeRadii.reserve(vSize);

                for (std::size_t i = 0; i < vSize; ++i)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (graph.IsDeleted(v))
                        continue;

                    positions.push_back(graph.VertexPosition(v));
                    // Graph nodes have no meaningful surface normal — use world-up default.
                    normals.emplace_back(0.0f, 1.0f, 0.0f);
                    remap[i] = compactIdx++;

                    // Extract per-node color (pack to ABGR uint32).
                    if (hasNodeColors && nodeColorProp)
                    {
                        const glm::vec4& c = (*nodeColorProp)[v];
                        nodeColors.push_back(Graphics::GpuColor::PackColorF(c.r, c.g, c.b, c.a));
                    }

                    // Extract per-node radius.
                    if (hasNodeRadii && nodeRadiusProp)
                    {
                        nodeRadii.push_back((*nodeRadiusProp)[v]);
                    }
                }

                if (positions.empty())
                {
                    if (graphData.GpuGeometry.IsValid())
                    {
                        geometryStorage.Remove(graphData.GpuGeometry, device->GetGlobalFrameNumber());
                        graphData.GpuGeometry = {};
                    }
                    if (graphData.GpuEdgeGeometry.IsValid())
                    {
                        geometryStorage.Remove(graphData.GpuEdgeGeometry, device->GetGlobalFrameNumber());
                        graphData.GpuEdgeGeometry = {};
                        graphData.GpuEdgeCount = 0;
                    }
                    graphData.CachedEdgePairs.clear();
                    graphData.CachedEdgeColors.clear();
                    graphData.CachedNodeColors.clear();
                    graphData.CachedNodeRadii.clear();
                    graphData.GpuVertexCount = 0;
                    graphData.GpuDirty = false;
                    continue;
                }

                // --- Extract edge pairs (remapped to compacted indices) ---
                const std::size_t eSize = graph.EdgesSize();
                std::vector<ECS::EdgePair> edgePairs;
                edgePairs.reserve(eSize);

                // Check for per-edge color property ("e:color" as glm::vec4).
                const bool hasEdgeColors = graph.EdgeProperties().Exists("e:color");
                std::optional<Geometry::EdgeProperty<glm::vec4>> edgeColorProp;
                if (hasEdgeColors)
                    edgeColorProp = Geometry::EdgeProperty<glm::vec4>(
                        graph.EdgeProperties().Get<glm::vec4>("e:color"));

                std::vector<uint32_t> edgeColors;
                if (hasEdgeColors)
                    edgeColors.reserve(eSize);

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

                    // Extract per-edge color (pack to ABGR uint32).
                    if (hasEdgeColors && edgeColorProp)
                    {
                        const glm::vec4& c = (*edgeColorProp)[e];
                        edgeColors.push_back(Graphics::GpuColor::PackColorF(c.r, c.g, c.b, c.a));
                    }
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
                    edgeReq.UploadMode = GeometryUploadMode::Direct;

                    auto [edgeGpuData, edgeToken] = GeometryGpuData::CreateAsync(
                        device, transferManager, edgeReq, &geometryStorage);

                    if (edgeGpuData && edgeGpuData->GetIndexBuffer())
                    {
                        graphData.GpuEdgeGeometry = geometryStorage.Add(std::move(edgeGpuData));
                        graphData.GpuEdgeCount = static_cast<uint32_t>(edgePairs.size());
                    }
                    else
                    {
                        Core::Log::Warn("GraphGeometrySync: Failed to create edge index buffer for entity {}",
                                        static_cast<uint32_t>(entity));
                    }
                }

                graphData.CachedEdgePairs = std::move(edgePairs);
                graphData.CachedEdgeColors = std::move(edgeColors);
                graphData.CachedNodeColors = std::move(nodeColors);
                graphData.CachedNodeRadii = std::move(nodeRadii);
                graphData.GpuVertexCount = compactIdx;
                graphData.GpuDirty = false;
            }

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // Same contract as PointCloudRendererLifecycle: allocate once, then
            // GPUSceneSync handles subsequent transform-only updates.
            // -----------------------------------------------------------------
            if (graphData.GpuSlot == ECS::Graph::Data::kInvalidSlot && graphData.GpuGeometry.IsValid())
            {
                GeometryGpuData* geo = geometryStorage.GetUnchecked(graphData.GpuGeometry);
                if (!geo || !geo->GetVertexBuffer())
                    continue;

                const uint32_t slot = gpuScene.AllocateSlot();
                if (slot == ECS::Graph::Data::kInvalidSlot)
                    continue;

                graphData.GpuSlot = slot;

                GpuInstanceData inst{};

                auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
                if (wm)
                    inst.Model = wm->Matrix;

                inst.GeometryID = graphData.GpuGeometry.Index;

                if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entity))
                    inst.EntityID = pick->Value;

                glm::vec4 sphere = ComputeLocalBoundingSphere(*geo);
                if (sphere.w <= 0.0f)
                    sphere.w = GPUSceneConstants::kMinBoundingSphereRadius;

                gpuScene.QueueUpdate(graphData.GpuSlot, inst, sphere);

                // Clear the WorldUpdatedTag so GPUSceneSync doesn't double-update
                // on the same frame.
                registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entity);
            }
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
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
            [&registry, &gpuScene, &geometryStorage, device, &transferManager]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, device, transferManager);
            });
    }
}

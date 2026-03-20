module;

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Graphics.Systems.MeshViewLifecycle;

import Graphics.GPUScene;
import Graphics.Components;
import Graphics.Geometry;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;

import ECS;

import RHI.Device;
import RHI.Transfer;

import Geometry.Handle;

#include "Graphics.LifecycleUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Systems::MeshViewLifecycle
{
    namespace
    {
        // Extract unique edges from triangle indices.
        // Returns flattened uint32_t indices: [i0_0, i1_0, i0_1, i1_1, ...]
        [[nodiscard]] auto ExtractUniqueEdgesFromTriangles(
            const std::vector<uint32_t>& triIndices) -> std::vector<uint32_t>
        {
            struct PairHash {
                std::size_t operator()(std::pair<uint32_t, uint32_t> p) const noexcept {
                    return std::hash<uint64_t>{}(
                        (static_cast<uint64_t>(p.first) << 32) | static_cast<uint64_t>(p.second));
                }
            };

            std::unordered_set<std::pair<uint32_t, uint32_t>, PairHash> edgeSet;
            edgeSet.reserve(triIndices.size());

            for (std::size_t t = 0; t + 2 < triIndices.size(); t += 3)
            {
                const uint32_t i0 = triIndices[t];
                const uint32_t i1 = triIndices[t + 1];
                const uint32_t i2 = triIndices[t + 2];

                // Skip degenerate (zero-area) triangles with duplicate vertex indices.
                if (i0 == i1 || i1 == i2 || i0 == i2)
                    continue;

                auto addEdge = [&](uint32_t a, uint32_t b) {
                    auto key = (a < b) ? std::pair{a, b} : std::pair{b, a};
                    edgeSet.insert(key);
                };

                addEdge(i0, i1);
                addEdge(i1, i2);
                addEdge(i2, i0);
            }

            std::vector<uint32_t> result;
            result.reserve(edgeSet.size() * 2);
            for (const auto& [a, b] : edgeSet)
            {
                result.push_back(a);
                result.push_back(b);
            }

            return result;
        }
    }

    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager,
                  entt::dispatcher& dispatcher)
    {
        // -----------------------------------------------------------------
        // Phase 0: Auto-attach/detach internal view components based on
        //          Line/Point::Component presence.
        // -----------------------------------------------------------------
        // Ensures MeshEdgeView::Component exists when Line::Component is
        // present, and MeshVertexView::Component exists when
        // Point::Component is present (on Surface entities only). Removed
        // when the corresponding per-pass component is detached. This runs
        // before Phase 1/2 so new views are processed in the same frame.
        {
            auto surfView = registry.view<ECS::Surface::Component>();

            for (auto [entity, sc] : surfView.each())
            {
                // Edge view ↔ Line::Component
                const bool wantEdges = registry.all_of<ECS::Line::Component>(entity);
                const bool hasEdgeView = registry.all_of<ECS::MeshEdgeView::Component>(entity);

                if (wantEdges && !hasEdgeView)
                    registry.emplace<ECS::MeshEdgeView::Component>(entity);
                else if (!wantEdges && hasEdgeView)
                    registry.remove<ECS::MeshEdgeView::Component>(entity);

                // Vertex view ↔ Point::Component
                const bool wantVertices = registry.all_of<ECS::Point::Component>(entity);
                const bool hasVertexView = registry.all_of<ECS::MeshVertexView::Component>(entity);

                if (wantVertices && !hasVertexView)
                    registry.emplace<ECS::MeshVertexView::Component>(entity);
                else if (!wantVertices && hasVertexView)
                    registry.remove<ECS::MeshVertexView::Component>(entity);
            }
        }

        // -----------------------------------------------------------------
        // Phase 1: Edge View Lifecycle
        // -----------------------------------------------------------------
        // Iterate entities with MeshEdgeView + Surface + MeshCollider.
        // When Dirty: extract unique edge pairs from collision data triangle
        // indices, create an edge index buffer via ReuseVertexBuffersFrom,
        // allocate GPUScene slot.

        auto edgeView = registry.view<ECS::MeshEdgeView::Component,
                                      ECS::Surface::Component>();

        for (auto [entity, ev, sc] : edgeView.each())
        {
            if (!ev.Dirty)
                continue;

            // Source mesh must have valid GPU geometry.
            if (!sc.Geometry.IsValid())
                continue;

            GeometryGpuData* srcGeo = geometryStorage.GetIfValid(sc.Geometry);
            if (!srcGeo || !srcGeo->GetVertexBuffer())
                continue;

            // Extract edge pairs directly from collision data (triangle indices).
            // The collision mesh indices reference into the same position array
            // as the GPU vertex buffer.
            auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
            if (!collider || !collider->CollisionRef || collider->CollisionRef->Indices.empty())
                continue;

            const auto& triIndices = collider->CollisionRef->Indices;

            // Extract unique edges from triangle indices.
            // Layout: [i0_0, i1_0, i0_1, i1_1, ...] — compatible with
            // Lines topology and BDA EdgePair reads.
            std::vector<uint32_t> indices = ExtractUniqueEdgesFromTriangles(triIndices);
            const uint32_t edgeCount = static_cast<uint32_t>(indices.size() / 2);

            if (edgeCount == 0)
            {
                ev.Dirty = false;
                continue;
            }

            GeometryUploadRequest req{};
            req.ReuseVertexBuffersFrom = sc.Geometry;
            req.Indices = indices;
            req.Topology = PrimitiveTopology::Lines;
            req.UploadMode = GeometryUploadMode::Direct;

            auto [gpuData, token] = GeometryGpuData::CreateAsync(
                device, transferManager, req, &geometryStorage);

            if (!gpuData || !gpuData->GetIndexBuffer())
            {
                HandleUploadFailure(dispatcher, entity, "MeshViewLifecycle");
                ev.Dirty = false;
                continue;
            }

            ev.Geometry = geometryStorage.Add(std::move(gpuData));
            ev.EdgeCount = edgeCount;
            ev.Dirty = false;

            // Allocate GPUScene slot for frustum culling.
            ev.GpuSlot = TryAllocateGpuSlot(
                registry, entity, gpuScene, geometryStorage,
                ev.GpuSlot, ev.Geometry);
        }

        // -----------------------------------------------------------------
        // Phase 1b: Populate Line::Component from completed edge views.
        // -----------------------------------------------------------------
        // Idempotent: runs every frame for all entities with valid edge
        // geometry, ensuring Line::Component always has current handles.
        // Replaces the ComponentMigration bridge for mesh wireframe.
        for (auto [entity, ev, sc] : edgeView.each())
        {
            if (!ev.Geometry.IsValid())
                continue;

            auto* lineComp = registry.try_get<ECS::Line::Component>(entity);
            if (lineComp)
            {
                lineComp->Geometry  = sc.Geometry;   // Shared vertex buffer (BDA)
                lineComp->EdgeView  = ev.Geometry;   // Edge index buffer
                lineComp->EdgeCount = ev.EdgeCount;
            }
        }

        // -----------------------------------------------------------------
        // Phase 2: Vertex View Lifecycle
        // -----------------------------------------------------------------
        // Iterate entities with MeshVertexView + Surface.
        // When Dirty: create a vertex point view via ReuseVertexBuffersFrom
        // (topology Points, no index buffer), allocate GPUScene slot.

        auto vtxView = registry.view<ECS::MeshVertexView::Component,
                                     ECS::Surface::Component>();

        for (auto [entity, pv, sc] : vtxView.each())
        {
            if (!pv.Dirty)
                continue;

            if (!sc.Geometry.IsValid())
                continue;

            GeometryGpuData* srcGeo = geometryStorage.GetIfValid(sc.Geometry);
            if (!srcGeo || !srcGeo->GetVertexBuffer())
                continue;

            // Derive vertex count from the source mesh layout.
            const auto& layout = srcGeo->GetLayout();
            const uint32_t vertexCount = static_cast<uint32_t>(
                layout.PositionsSize / sizeof(glm::vec3));
            if (vertexCount == 0)
                continue;

            // Create vertex view — shares vertex buffer, no index buffer.
            GeometryUploadRequest req{};
            req.ReuseVertexBuffersFrom = sc.Geometry;
            req.Topology = PrimitiveTopology::Points;
            req.UploadMode = GeometryUploadMode::Direct;

            auto [gpuData, token] = GeometryGpuData::CreateAsync(
                device, transferManager, req, &geometryStorage);

            if (!gpuData || !gpuData->GetVertexBuffer())
            {
                HandleUploadFailure(dispatcher, entity, "MeshViewLifecycle");
                pv.Dirty = false;
                continue;
            }

            pv.Geometry = geometryStorage.Add(std::move(gpuData));
            pv.VertexCount = vertexCount;
            pv.Dirty = false;

            // Allocate GPUScene slot for frustum culling.
            pv.GpuSlot = TryAllocateGpuSlot(
                registry, entity, gpuScene, geometryStorage,
                pv.GpuSlot, pv.Geometry);
        }

        // -----------------------------------------------------------------
        // Phase 2b: Populate Point::Component from completed vertex views.
        // -----------------------------------------------------------------
        // Idempotent: runs every frame for all entities with valid vertex
        // view geometry, ensuring Point::Component always has current handles.
        for (auto [entity, pv, sc] : vtxView.each())
        {
            if (!pv.Geometry.IsValid())
                continue;

            auto* pointComp = registry.try_get<ECS::Point::Component>(entity);
            if (pointComp)
            {
                pointComp->Geometry = pv.Geometry;  // Vertex view (Points topology)

                // Propagate normals availability from source mesh.
                GeometryGpuData* geo = geometryStorage.GetIfValid(pv.Geometry);
                if (geo)
                    pointComp->HasPerPointNormals = (geo->GetLayout().NormalsSize > 0);
            }
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
        graph.AddPass("MeshViewLifecycle",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<ECS::Components::Transform::WorldMatrix>();
                builder.Write<ECS::MeshEdgeView::Component>();
                builder.Write<ECS::MeshVertexView::Component>();
                builder.Write<ECS::Line::Component>();
                builder.Write<ECS::Point::Component>();
                builder.WaitFor("MeshRendererLifecycle"_id);
            },
            [&registry, &gpuScene, &geometryStorage, device, &transferManager, &dispatcher]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, device, transferManager, dispatcher);
            });
    }
}

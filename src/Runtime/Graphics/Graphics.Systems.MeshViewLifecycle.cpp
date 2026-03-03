module;

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.MeshViewLifecycle.Impl;

import :Systems.MeshViewLifecycle;
import :GPUScene;
import :Components;
import :Geometry;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import ECS;
import Geometry;
import RHI;

using namespace Core::Hash;

namespace Graphics::Systems::MeshViewLifecycle
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
                  RHI::TransferManager& transferManager)
    {
        // -----------------------------------------------------------------
        // Phase 0: Auto-attach/detach MeshEdgeView based on Line::Component
        // -----------------------------------------------------------------
        // Ensures MeshEdgeView::Component exists when Line::Component is
        // present, and is removed when it is absent. This runs before
        // Phase 1 so that new edge views are processed in the same frame.
        {
            auto surfView = registry.view<ECS::Surface::Component>();

            for (auto [entity, sc] : surfView.each())
            {
                const bool wantEdges = registry.all_of<ECS::Line::Component>(entity);
                const bool hasEdgeView = registry.all_of<ECS::MeshEdgeView::Component>(entity);

                if (wantEdges && !hasEdgeView)
                    registry.emplace<ECS::MeshEdgeView::Component>(entity);
                else if (!wantEdges && hasEdgeView)
                    registry.remove<ECS::MeshEdgeView::Component>(entity);
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

            GeometryGpuData* srcGeo = geometryStorage.GetUnchecked(sc.Geometry);
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
                Core::Log::Error("MeshViewLifecycle: Failed to create edge view for entity {}",
                                 static_cast<uint32_t>(entity));
                ev.Dirty = false;
                continue;
            }

            ev.Geometry = geometryStorage.Add(std::move(gpuData));
            ev.EdgeCount = edgeCount;
            ev.Dirty = false;

            // Allocate GPUScene slot for frustum culling.
            if (ev.GpuSlot == ECS::MeshEdgeView::Component::kInvalidSlot)
            {
                const uint32_t slot = gpuScene.AllocateSlot();
                if (slot == ECS::MeshEdgeView::Component::kInvalidSlot)
                    continue;

                ev.GpuSlot = slot;

                GpuInstanceData inst{};
                auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
                if (wm)
                    inst.Model = wm->Matrix;
                inst.GeometryID = ev.Geometry.Index;

                GeometryGpuData* edgeGeo = geometryStorage.GetUnchecked(ev.Geometry);
                glm::vec4 sphere = edgeGeo
                    ? ComputeLocalBoundingSphere(*edgeGeo)
                    : glm::vec4(0.0f, 0.0f, 0.0f, GPUSceneConstants::kMinBoundingSphereRadius);
                if (sphere.w <= 0.0f)
                    sphere.w = GPUSceneConstants::kMinBoundingSphereRadius;

                gpuScene.QueueUpdate(ev.GpuSlot, inst, sphere);
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

            GeometryGpuData* srcGeo = geometryStorage.GetUnchecked(sc.Geometry);
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
                Core::Log::Error("MeshViewLifecycle: Failed to create vertex view for entity {}",
                                 static_cast<uint32_t>(entity));
                pv.Dirty = false;
                continue;
            }

            pv.Geometry = geometryStorage.Add(std::move(gpuData));
            pv.VertexCount = vertexCount;
            pv.Dirty = false;

            // Allocate GPUScene slot for frustum culling.
            if (pv.GpuSlot == ECS::MeshVertexView::Component::kInvalidSlot)
            {
                const uint32_t slot = gpuScene.AllocateSlot();
                if (slot == ECS::MeshVertexView::Component::kInvalidSlot)
                    continue;

                pv.GpuSlot = slot;

                GpuInstanceData inst{};
                auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
                if (wm)
                    inst.Model = wm->Matrix;
                inst.GeometryID = pv.Geometry.Index;

                GeometryGpuData* vtxGeo = geometryStorage.GetUnchecked(pv.Geometry);
                glm::vec4 sphere = vtxGeo
                    ? ComputeLocalBoundingSphere(*vtxGeo)
                    : glm::vec4(0.0f, 0.0f, 0.0f, GPUSceneConstants::kMinBoundingSphereRadius);
                if (sphere.w <= 0.0f)
                    sphere.w = GPUSceneConstants::kMinBoundingSphereRadius;

                gpuScene.QueueUpdate(pv.GpuSlot, inst, sphere);
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
        graph.AddPass("MeshViewLifecycle",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<ECS::Components::Transform::WorldMatrix>();
                builder.Write<ECS::MeshEdgeView::Component>();
                builder.Write<ECS::MeshVertexView::Component>();
                builder.WaitFor("MeshRendererLifecycle"_id);
            },
            [&registry, &gpuScene, &geometryStorage, device, &transferManager]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, device, transferManager);
            });
    }
}

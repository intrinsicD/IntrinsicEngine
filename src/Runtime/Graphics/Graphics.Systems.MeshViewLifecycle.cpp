module;

#include <cstdint>
#include <memory>
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
    }

    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager)
    {
        // -----------------------------------------------------------------
        // Phase 1: Edge View Lifecycle
        // -----------------------------------------------------------------
        // Iterate entities with MeshEdgeView + MeshRenderer.
        // When Dirty: extract edge pairs from RenderVisualization::CachedEdges,
        // create an edge index buffer via ReuseVertexBuffersFrom, allocate
        // GPUScene slot.

        auto edgeView = registry.view<ECS::MeshEdgeView::Component,
                                      ECS::MeshRenderer::Component>();

        for (auto [entity, ev, mr] : edgeView.each())
        {
            if (!ev.Dirty)
                continue;

            // Source mesh must have valid GPU geometry.
            if (!mr.Geometry.IsValid())
                continue;

            GeometryGpuData* srcGeo = geometryStorage.GetUnchecked(mr.Geometry);
            if (!srcGeo || !srcGeo->GetVertexBuffer())
                continue;

            // Edge pairs come from RenderVisualization::CachedEdges (populated
            // by MeshRenderPass when ShowWireframe is enabled).
            auto* vis = registry.try_get<ECS::RenderVisualization::Component>(entity);
            if (!vis || vis->CachedEdges.empty())
                continue;

            // Flatten EdgePair array to contiguous uint32_t indices.
            // Layout: [i0_0, i1_0, i0_1, i1_1, ...] — compatible with
            // Lines topology and BDA EdgePair reads.
            const uint32_t edgeCount = static_cast<uint32_t>(vis->CachedEdges.size());
            std::vector<uint32_t> indices;
            indices.reserve(static_cast<size_t>(edgeCount) * 2);
            for (const auto& [i0, i1] : vis->CachedEdges)
            {
                indices.push_back(i0);
                indices.push_back(i1);
            }

            GeometryUploadRequest req{};
            req.ReuseVertexBuffersFrom = mr.Geometry;
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
        // Iterate entities with MeshVertexView + MeshRenderer.
        // When Dirty: create a vertex point view via ReuseVertexBuffersFrom
        // (topology Points, no index buffer), allocate GPUScene slot.

        auto vtxView = registry.view<ECS::MeshVertexView::Component,
                                     ECS::MeshRenderer::Component>();

        for (auto [entity, pv, mr] : vtxView.each())
        {
            if (!pv.Dirty)
                continue;

            if (!mr.Geometry.IsValid())
                continue;

            GeometryGpuData* srcGeo = geometryStorage.GetUnchecked(mr.Geometry);
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
            req.ReuseVertexBuffersFrom = mr.Geometry;
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

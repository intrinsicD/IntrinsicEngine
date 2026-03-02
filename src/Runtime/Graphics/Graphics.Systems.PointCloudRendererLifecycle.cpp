module;

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.PointCloudRendererLifecycle.Impl;

import :Systems.PointCloudRendererLifecycle;
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

namespace Graphics::Systems::PointCloudRendererLifecycle
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
        auto view = registry.view<ECS::PointCloudRenderer::Component>();

        for (auto [entity, pc] : view.each())
        {
            // -----------------------------------------------------------------
            // Phase 1: Upload CPU data to GPU (first frame only).
            // -----------------------------------------------------------------
            if (pc.GpuDirty && !pc.Geometry.IsValid() && !pc.Positions.empty())
            {
                // Build normals if missing (default up vector).
                std::vector<glm::vec3> normals;
                if (pc.HasNormals())
                {
                    normals = pc.Normals;
                }
                else
                {
                    normals.resize(pc.Positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
                }

                GeometryUploadRequest upload{};
                upload.Positions = pc.Positions;
                upload.Normals = normals;
                upload.Topology = PrimitiveTopology::Points;
                upload.UploadMode = GeometryUploadMode::Staged;

                auto [gpuData, token] = GeometryGpuData::CreateAsync(
                    device, transferManager, upload, &geometryStorage);

                if (!gpuData || !gpuData->GetVertexBuffer())
                {
                    Core::Log::Error("PointCloudRendererLifecycle: Failed to upload point cloud for entity {}",
                                     static_cast<uint32_t>(entity));
                    pc.GpuDirty = false;
                    continue;
                }

                pc.Geometry = geometryStorage.Add(std::move(gpuData));

                // Free CPU data — it is now on the GPU.
                pc.Positions.clear();
                pc.Positions.shrink_to_fit();
                pc.Normals.clear();
                pc.Normals.shrink_to_fit();
                pc.Colors.clear();
                pc.Colors.shrink_to_fit();
                pc.Radii.clear();
                pc.Radii.shrink_to_fit();

                pc.GpuDirty = false;
            }
            else if (pc.GpuDirty && pc.Geometry.IsValid())
            {
                // Handle already assigned (e.g. from ModelLoader) — just clear dirty flag.
                pc.GpuDirty = false;
            }

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // -----------------------------------------------------------------
            if (pc.GpuSlot == ECS::PointCloudRenderer::Component::kInvalidSlot && pc.Geometry.IsValid())
            {
                GeometryGpuData* geo = geometryStorage.GetUnchecked(pc.Geometry);
                if (!geo || !geo->GetVertexBuffer())
                    continue;

                const uint32_t slot = gpuScene.AllocateSlot();
                if (slot == ECS::PointCloudRenderer::Component::kInvalidSlot)
                    continue;

                pc.GpuSlot = slot;

                GpuInstanceData inst{};

                auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
                if (wm)
                    inst.Model = wm->Matrix;

                inst.GeometryID = pc.Geometry.Index;

                if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entity))
                    inst.EntityID = pick->Value;

                glm::vec4 sphere = ComputeLocalBoundingSphere(*geo);
                if (sphere.w <= 0.0f)
                    sphere.w = GPUSceneConstants::kMinBoundingSphereRadius;

                gpuScene.QueueUpdate(pc.GpuSlot, inst, sphere);

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
        graph.AddPass("PointCloudRendererLifecycle",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Write<ECS::PointCloudRenderer::Component>();
                builder.WaitFor("TransformUpdate"_id);
            },
            [&registry, &gpuScene, &geometryStorage, device, &transferManager]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, device, transferManager);
            });
    }
}

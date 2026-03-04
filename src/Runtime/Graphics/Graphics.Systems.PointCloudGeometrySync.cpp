module;

#include <span>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.PointCloudGeometrySync.Impl;

import :Systems.PointCloudGeometrySync;
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

namespace Graphics::Systems::PointCloudGeometrySync
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
        auto view = registry.view<ECS::PointCloud::Data>();

        for (auto [entity, pcData] : view.each())
        {
            // -----------------------------------------------------------------
            // Phase 1: Re-upload geometry when dirty.
            // -----------------------------------------------------------------
            if (pcData.GpuDirty)
            {
                if (!pcData.CloudRef || pcData.CloudRef->Empty())
                {
                    // Cloud is empty or null — release any existing GPU geometry.
                    if (pcData.GpuGeometry.IsValid())
                    {
                        geometryStorage.Remove(pcData.GpuGeometry, device->GetGlobalFrameNumber());
                        pcData.GpuGeometry = {};
                    }
                    pcData.CachedColors.clear();
                    pcData.CachedRadii.clear();
                    pcData.GpuPointCount = 0;
                    pcData.GpuDirty = false;
                    // Fall through to Phase 3 (GpuGeometry invalid → skip).
                }
                else
                {

                auto& cloud = *pcData.CloudRef;

                // --- Read positions and normals directly from Cloud spans ---
                // Cloud provides contiguous span accessors for zero-copy upload.
                const auto positions = cloud.Positions();

                // Build normals: use Cloud's normals if present, otherwise default up.
                std::vector<glm::vec3> defaultNormals;
                std::span<const glm::vec3> normals;

                if (cloud.HasNormals())
                {
                    normals = cloud.Normals();
                }
                else
                {
                    defaultNormals.resize(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
                    normals = defaultNormals;
                }

                // --- Extract per-point colors from PropertySet ---
                std::vector<uint32_t> pointColors;
                if (cloud.HasColors())
                {
                    const auto colors = cloud.Colors();
                    pointColors.reserve(colors.size());
                    for (const auto& c : colors)
                    {
                        pointColors.push_back(GpuColor::PackColorF(c.r, c.g, c.b, c.a));
                    }
                }

                // --- Extract per-point radii from PropertySet ---
                std::vector<float> pointRadii;
                if (cloud.HasRadii())
                {
                    const auto radii = cloud.Radii();
                    pointRadii.assign(radii.begin(), radii.end());
                }

                // Release previous geometry before allocating new.
                if (pcData.GpuGeometry.IsValid())
                {
                    geometryStorage.Remove(pcData.GpuGeometry, device->GetGlobalFrameNumber());
                    pcData.GpuGeometry = {};
                }

                // --- Upload to GPU via Staged mode (device-local for static clouds) ---
                // Cloud data is borrowed from the shared instance; Staged mode
                // copies to a staging belt then transfers to device-local memory,
                // optimal for clouds that don't change every frame.
                GeometryUploadRequest upload{};
                upload.Positions = positions;
                upload.Normals = normals;
                upload.Topology = PrimitiveTopology::Points;
                upload.UploadMode = GeometryUploadMode::Staged;

                auto [newGpuData, token] = GeometryGpuData::CreateAsync(
                    device, transferManager, upload, &geometryStorage);

                if (!newGpuData || !newGpuData->GetVertexBuffer())
                {
                    Core::Log::Error("PointCloudGeometrySync: Failed to create GPU geometry for entity {}",
                                     static_cast<uint32_t>(entity));
                    pcData.GpuDirty = false;
                    // Fall through to Phase 3 (GpuGeometry invalid → skip).
                }
                else
                {

                pcData.GpuGeometry = geometryStorage.Add(std::move(newGpuData));
                pcData.CachedColors = std::move(pointColors);
                pcData.CachedRadii = std::move(pointRadii);
                pcData.GpuPointCount = static_cast<uint32_t>(positions.size());
                pcData.GpuDirty = false;

                } // else (upload succeeded)
                } // else (cloud not empty)
            } // if (GpuDirty)

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // Same contract as PointCloudRendererLifecycle: allocate once, then
            // GPUSceneSync handles subsequent transform-only updates.
            // -----------------------------------------------------------------
            if (pcData.GpuSlot == ECS::PointCloud::Data::kInvalidSlot && pcData.GpuGeometry.IsValid())
            {
                GeometryGpuData* geo = geometryStorage.GetUnchecked(pcData.GpuGeometry);
                if (geo && geo->GetVertexBuffer())
                {
                    const uint32_t slot = gpuScene.AllocateSlot();
                    if (slot != ECS::PointCloud::Data::kInvalidSlot)
                    {
                        pcData.GpuSlot = slot;

                        GpuInstanceData inst{};

                        auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
                        if (wm)
                            inst.Model = wm->Matrix;

                        inst.GeometryID = pcData.GpuGeometry.Index;

                        if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entity))
                            inst.EntityID = pick->Value;

                        glm::vec4 sphere = ComputeLocalBoundingSphere(*geo);
                        if (sphere.w <= 0.0f)
                            sphere.w = GPUSceneConstants::kMinBoundingSphereRadius;

                        gpuScene.QueueUpdate(pcData.GpuSlot, inst, sphere);

                        // Clear the WorldUpdatedTag so GPUSceneSync doesn't double-update
                        // on the same frame.
                        registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entity);
                    }
                }
            }

            // -----------------------------------------------------------------
            // Phase 3: Populate Point::Component from PointCloud::Data.
            // -----------------------------------------------------------------
            // Directly populates Point::Component, replacing
            // ComponentMigration's point cloud bridging. Idempotent — runs
            // every frame for all visible cloud entities with valid GPU geometry.
            if (pcData.Visible && pcData.GpuGeometry.IsValid())
            {
                auto& pt = registry.get_or_emplace<ECS::Point::Component>(entity);
                pt.Geometry          = pcData.GpuGeometry;
                pt.Color             = pcData.DefaultColor;
                pt.Size              = pcData.DefaultRadius;
                pt.SizeMultiplier    = pcData.SizeMultiplier;
                pt.Mode              = pcData.RenderMode;
                pt.HasPerPointColors = pcData.HasColors();
                pt.HasPerPointRadii  = pcData.HasRadii();
                pt.HasPerPointNormals = pcData.HasNormals();
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
        graph.AddPass("PointCloudGeometrySync",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Write<ECS::PointCloud::Data>();
                builder.Write<ECS::Point::Component>();
                builder.WaitFor("TransformUpdate"_id);
                builder.WaitFor("PropertySetDirtySync"_id);
            },
            [&registry, &gpuScene, &geometryStorage, device, &transferManager]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, device, transferManager);
            });
    }
}

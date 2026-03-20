module;

#include <span>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Graphics.Systems.PointCloudGeometrySync;

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

import RHI.Device;
import RHI.Transfer;

import Geometry.PointCloud;
import Geometry.Handle;

#include "Graphics.PointCloudPropertyHelpers.hpp"
#include "Graphics.LifecycleUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Systems::PointCloudGeometrySync
{
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager,
                  entt::dispatcher& dispatcher)
    {
        auto view = registry.view<ECS::PointCloud::Data>();

        for (auto [entity, pcData] : view.each())
        {
            // -----------------------------------------------------------------
            // Phase 1: Re-upload geometry when dirty.
            // -----------------------------------------------------------------
            if (pcData.GpuDirty)
            {
                // Preloaded geometry path: keep existing GPU geometry and infer
                // normals from uploaded layout when CloudRef is absent.
                if (pcData.GpuGeometry.IsValid() && !pcData.CloudRef)
                {
                    if (GeometryGpuData* geo = geometryStorage.GetIfValid(pcData.GpuGeometry))
                        pcData.HasGpuNormals = (geo->GetLayout().NormalsSize > 0);
                    pcData.GpuDirty = false;
                }
                else if (!pcData.CloudRef || pcData.CloudRef->IsEmpty())
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
                    pcData.HasGpuNormals = false;
                    pcData.GpuDirty = false;
                    // Fall through to Phase 3 (GpuGeometry invalid -> skip).
                }
                else
                {

                auto& cloud = *pcData.CloudRef;

                // --- Read positions and normals directly from Cloud spans ---
                // Cloud provides contiguous span accessors for zero-copy upload.
                const auto positions = cloud.Positions();

                // Surfel/EWA require real normals; no synthetic default-up normals.
                const bool hasNormals = cloud.HasNormals();

                auto pointColors = PointCloudPropertyHelpers::ExtractPointColors(
                    cloud, pcData.Visualization.VertexColors);
                auto pointRadii = PointCloudPropertyHelpers::ExtractPointRadii(cloud);

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
                if (hasNormals)
                    upload.Normals = cloud.Normals();
                upload.Topology = PrimitiveTopology::Points;
                upload.UploadMode = GeometryUploadMode::Staged;

                auto [newGpuData, token] = GeometryGpuData::CreateAsync(
                    device, transferManager, upload, &geometryStorage);

                if (!newGpuData || !newGpuData->GetVertexBuffer())
                {
                    HandleUploadFailure(dispatcher, entity, "PointCloudGeometrySync");
                    pcData.CachedColors.clear();
                    pcData.CachedRadii.clear();
                    pcData.GpuPointCount = 0;
                    pcData.HasGpuNormals = false;
                    pcData.GpuDirty = false;
                }
                else
                {

                pcData.GpuGeometry = geometryStorage.Add(std::move(newGpuData));
                pcData.CachedColors = std::move(pointColors);
                pcData.CachedRadii = std::move(pointRadii);
                pcData.GpuPointCount = static_cast<uint32_t>(positions.size());
                pcData.HasGpuNormals = hasNormals;
                pcData.GpuDirty = false;

                } // else (upload succeeded)
                } // else (cloud not empty)
            } // if (GpuDirty)

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // Allocate once, then GPUSceneSync handles subsequent transform-only updates.
            // -----------------------------------------------------------------
            pcData.GpuSlot = TryAllocateGpuSlot(
                registry, entity, gpuScene, geometryStorage,
                pcData.GpuSlot, pcData.GpuGeometry);

            // -----------------------------------------------------------------
            // Phase 3: Populate Point::Component from PointCloud::Data.
            // -----------------------------------------------------------------
            PopulateOrRemovePassComponent<ECS::Point::Component>(
                registry, entity, pcData.Visible, pcData.GpuGeometry.IsValid(),
                [&](ECS::Point::Component& pt) {
                    pt.Geometry           = pcData.GpuGeometry;
                    pt.Color              = pcData.DefaultColor;
                    pt.Size               = pcData.DefaultRadius;
                    pt.SizeMultiplier     = pcData.SizeMultiplier;
                    pt.Mode               = pcData.RenderMode;
                    pt.HasPerPointColors  = !pcData.CachedColors.empty();
                    pt.HasPerPointRadii   = pcData.HasRadii();
                    pt.HasPerPointNormals = pcData.HasRenderableNormals();
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
        graph.AddPass("PointCloudGeometrySync",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Write<ECS::PointCloud::Data>();
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

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
import :ColorMapper;
import :VisualizationConfig;

import Core.Hash;
import Core.Logging;
import Core.FrameGraph;
import ECS;
import Geometry;
import RHI;

#include "Graphics.LifecycleUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Systems::PointCloudGeometrySync
{
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

                // --- Extract per-point colors via ColorMapper ---
                std::vector<uint32_t> pointColors;
                {
                    auto& vtxConfig = pcData.Visualization.VertexColors;
                    // Default fallback: use "p:color" when no property is explicitly selected.
                    if (vtxConfig.PropertyName.empty() && cloud.HasColors())
                        vtxConfig.PropertyName = "p:color";

                    if (auto mapped = ColorMapper::MapProperty(
                            cloud.PointProperties(), vtxConfig))
                    {
                        pointColors = std::move(mapped->Colors);
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
                if (hasNormals)
                    upload.Normals = cloud.Normals();
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
                pcData.HasGpuNormals = hasNormals;
                pcData.GpuDirty = false;

                } // else (upload succeeded)
                } // else (cloud not empty)
            } // if (GpuDirty)

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // Allocate once, then GPUSceneSync handles subsequent transform-only updates.
            // -----------------------------------------------------------------
            if (pcData.GpuSlot == ECS::PointCloud::Data::kInvalidSlot && pcData.GpuGeometry.IsValid())
            {
                GeometryGpuData* geo = geometryStorage.GetIfValid(pcData.GpuGeometry);
                if (geo && geo->GetVertexBuffer())
                {
                    const uint32_t slot = AllocateGpuSlot(
                        registry, entity, gpuScene, *geo, pcData.GpuGeometry);
                    if (slot != ECS::PointCloud::Data::kInvalidSlot)
                        pcData.GpuSlot = slot;
                }
            }

            // -----------------------------------------------------------------
            // Phase 3: Populate Point::Component from PointCloud::Data.
            // -----------------------------------------------------------------
            // Idempotent: runs every frame for visible cloud entities with
            // valid GPU geometry.
            if (pcData.Visible && pcData.GpuGeometry.IsValid())
            {
                auto& pt = registry.get_or_emplace<ECS::Point::Component>(entity);
                pt.Geometry           = pcData.GpuGeometry;
                pt.Color              = pcData.DefaultColor;
                pt.Size               = pcData.DefaultRadius;
                pt.SizeMultiplier     = pcData.SizeMultiplier;
                pt.Mode               = pcData.RenderMode;
                pt.HasPerPointColors  = !pcData.CachedColors.empty();
                pt.HasPerPointRadii   = pcData.HasRadii();
                pt.HasPerPointNormals = pcData.HasRenderableNormals();
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

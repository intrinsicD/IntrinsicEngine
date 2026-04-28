module;

#include <cassert>
#include <span>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Graphics.Systems.PointCloudLifecycle;

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
import Geometry.Properties;
import Geometry.Handle;
import Core.SystemFeatureCatalog;
import Graphics.LifecycleUtils;

#include "Graphics.PointCloudPropertyHelpers.hpp"

using namespace Core::Hash;
using namespace Graphics::LifecycleUtils;

namespace Graphics::Systems::PointCloudLifecycle
{
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  RHI::BufferManager& bufferManager,
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
                else
                {
                    // Sync GeometrySources from CloudRef when it is available.
                    // CloudRef is an optional computation tool — after this call,
                    // GeometrySources::Vertices is the authoritative CPU data.
                    if (pcData.CloudRef && !pcData.CloudRef->IsEmpty())
                    {
                        ECS::Components::GeometrySources::PopulateFromCloud(
                            registry, entity, *pcData.CloudRef);
                    }

                    // Read from authoritative GeometrySources.
                    auto* geoVerts = registry.try_get<ECS::Components::GeometrySources::Vertices>(entity);

                    if (!geoVerts)
                    {
                        // No data source — release any existing GPU geometry.
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
                        // Fall through to Phase 3 (GpuGeometry invalid → skip).
                    }
                    else
                    {
                        auto posProp = geoVerts->Properties.Get<glm::vec3>("v:position");

                        if (!posProp || posProp.Vector().empty())
                        {
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
                        }
                        else
                        {
                            const auto& posVec = posProp.Vector();
                            const std::span<const glm::vec3> positions{posVec};

                            // Check for canonical normal property.
                            auto normProp = geoVerts->Properties.Get<glm::vec3>("v:normal");
                            const bool hasNormals = normProp && !normProp.Vector().empty();

                            // Extract per-point colors and radii from GeometrySources.
                            auto pointColors = PointCloudPropertyHelpers::ExtractPointColorsFromPropertySet(
                                geoVerts->Properties, pcData.Visualization.VertexColors);
                            auto pointRadii = PointCloudPropertyHelpers::ExtractPointRadiiFromPropertySet(
                                geoVerts->Properties);

                            // Release previous geometry before allocating new.
                            if (pcData.GpuGeometry.IsValid())
                            {
                                geometryStorage.Remove(pcData.GpuGeometry, device->GetGlobalFrameNumber());
                                pcData.GpuGeometry = {};
                            }

                            GeometryUploadRequest upload{};
                            upload.Positions = positions;
                            if (hasNormals)
                                upload.Normals = std::span<const glm::vec3>{normProp.Vector()};
                            upload.Topology = PrimitiveTopology::Points;
                            upload.UploadMode = GeometryUploadMode::Staged;

                            auto [newGpuData, token] = GeometryGpuData::CreateAsync(
                                device, transferManager, bufferManager, upload, &geometryStorage);

                            if (!newGpuData || !newGpuData->GetVertexBuffer())
                            {
                                HandleUploadFailure(dispatcher, entity, "PointCloudLifecycle");
                                pcData.CachedColors.clear();
                                pcData.CachedRadii.clear();
                                pcData.GpuPointCount = 0;
                                pcData.HasGpuNormals = false;
                                pcData.GpuDirty = false;
                            }
                            else
                            {
                                pcData.GpuGeometry = geometryStorage.Add(std::move(*newGpuData));
                                pcData.CachedColors = std::move(pointColors);
                                pcData.CachedRadii = std::move(pointRadii);
                                pcData.GpuPointCount = static_cast<uint32_t>(positions.size());
                                pcData.HasGpuNormals = hasNormals;
                                pcData.GpuDirty = false;
                            }
                        }
                    }
                }
            } // if (GpuDirty)

            // -----------------------------------------------------------------
            // Phase 2: Allocate GPUScene slot for entities with valid GPU geometry.
            // Allocate once, then GPUSceneSync handles subsequent transform-only updates.
            // -----------------------------------------------------------------
            pcData.GpuSlot = TryAllocateGpuSlot(
                registry, entity, gpuScene, geometryStorage,
                pcData.GpuSlot, pcData.GpuGeometry);

            // Ensure DataAuthority tag is present (single-authority invariant).
            if (!registry.all_of<ECS::DataAuthority::PointCloudTag>(entity))
            {
                assert((!registry.any_of<ECS::DataAuthority::MeshTag, ECS::DataAuthority::GraphTag>(entity))
                        && "Entity already has a different DataAuthority tag");
                registry.emplace<ECS::DataAuthority::PointCloudTag>(entity);
            }

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
                    pt.HasPerPointRadii   = !pcData.CachedRadii.empty();
                    pt.HasPerPointNormals = pcData.HasRenderableNormals();
                    pt.SourceDomain       = ECS::Point::Domain::CloudPoint;
                });
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        RHI::BufferManager& bufferManager,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager,
                        entt::dispatcher& dispatcher)
    {
        graph.AddPass(Runtime::SystemFeatureCatalog::PassNames::PointCloudLifecycle,
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Write<ECS::PointCloud::Data>();
                builder.Write<ECS::Point::Component>();
                builder.WaitFor("TransformUpdate"_id);
                builder.WaitFor("PropertySetDirtySync"_id);
            },
            [&registry, &gpuScene, &geometryStorage, &bufferManager, device, &transferManager, &dispatcher]()
            {
                OnUpdate(registry, gpuScene, geometryStorage, bufferManager, device, transferManager, dispatcher);
            });
    }
}

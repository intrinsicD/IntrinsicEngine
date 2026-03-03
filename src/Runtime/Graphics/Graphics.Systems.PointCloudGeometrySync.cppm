module;

#include <memory>
#include <entt/fwd.hpp>

export module Graphics:Systems.PointCloudGeometrySync;

import :Geometry;
import :GPUScene;
import Core.FrameGraph;
import RHI;

export namespace Graphics::Systems::PointCloudGeometrySync
{
    // Uploads PointCloud::Cloud positions/normals to device-local vertex
    // buffers, extracts per-point attributes (colors, radii) from
    // PropertySets, and allocates GPUScene slots for frustum culling —
    // for retained-mode BDA rendering.
    //
    // Contract:
    //  - Iterates entities with ECS::PointCloud::Data where GpuDirty == true.
    //  - Reads positions and normals directly from Cloud spans (zero copy).
    //  - Creates GeometryGpuData via Staged upload (device-local for static clouds).
    //  - Extracts per-point colors ("p:color") and radii ("p:radius") from
    //    Cloud PropertySets into CachedColors / CachedRadii.
    //  - Stores the GeometryHandle and point attributes on the component.
    //  - Allocates a GPUScene slot after successful upload (if not yet allocated),
    //    queues initial instance data with bounding sphere for frustum culling.
    //  - Clears GpuDirty after successful upload.
    //
    // Thread model: main thread only (writes ECS components + GPU resources).
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Write<ECS::PointCloud::Data>, WaitFor("TransformUpdate").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager);
}

module;

#include <memory>
#include <entt/fwd.hpp>

export module Graphics:Systems.PointCloudRendererLifecycle;

import :Geometry;
import :GPUScene;
import Core.FrameGraph;
import RHI;

export namespace Graphics::Systems::PointCloudRendererLifecycle
{
    // Lifecycle glue for standalone point cloud entities.
    //
    // Contract:
    //  - Iterates entities with PointCloudRenderer::Component.
    //  - If GpuDirty and CPU positions are present: uploads positions/normals to
    //    device-local via GeometryGpuData::CreateAsync(Staged), stores handle,
    //    clears GpuDirty and frees CPU vectors.
    //  - If Geometry is valid but GpuSlot is unallocated: allocates a GPUScene slot,
    //    pushes initial instance packet with bounding sphere.
    //  - On entity destruction: deactivates bounds and frees the GPUScene slot.
    //
    // Thread model: main thread only (writes ECS components + GPU resources).
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Write<PointCloudRenderer::Component>, WaitFor("TransformUpdate").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager);
}

module;

#include <memory>
#include <entt/fwd.hpp>

export module Graphics:Systems.GraphGeometrySync;

import :Geometry;
import Core.FrameGraph;
import RHI;

export namespace Graphics::Systems::GraphGeometrySync
{
    // Uploads graph node positions to device-local vertex buffers and extracts
    // edge index pairs from graph topology for retained-mode BDA rendering.
    //
    // Contract:
    //  - Iterates entities with ECS::Graph::Data where GpuDirty == true.
    //  - Compacts positions (skips deleted vertices, builds remap table).
    //  - Creates GeometryGpuData via Direct upload (CPU_TO_GPU for dynamic graphs).
    //  - Stores the GeometryHandle and edge pairs on the component.
    //  - Clears GpuDirty after successful upload.
    //
    // Thread model: main thread only (writes ECS components + GPU resources).
    void OnUpdate(entt::registry& registry,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Write<ECS::Graph::Data>, WaitFor("TransformUpdate").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager);
}

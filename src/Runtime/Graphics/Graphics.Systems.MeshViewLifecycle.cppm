module;

#include <memory>
#include <entt/fwd.hpp>

export module Graphics:Systems.MeshViewLifecycle;

import :Geometry;
import :GPUScene;
import Core.FrameGraph;
import RHI;

export namespace Graphics::Systems::MeshViewLifecycle
{
    // Lifecycle glue for mesh-derived geometry views (edge + vertex).
    //
    // Contract:
    //  - Iterates entities with MeshEdgeView::Component + MeshRenderer.
    //    If Dirty and CachedEdges are available: creates an edge index buffer
    //    via ReuseVertexBuffersFrom(meshHandle), stores the handle, allocates
    //    a GPUScene slot, and clears Dirty.
    //
    //  - Iterates entities with MeshVertexView::Component + MeshRenderer.
    //    If Dirty: creates a vertex view via ReuseVertexBuffersFrom(meshHandle)
    //    with Topology::Points, stores the handle, allocates a GPUScene slot,
    //    and clears Dirty.
    //
    //  - On entity destruction: GPUScene slots are freed by on_destroy hooks
    //    registered in SceneManager.
    //
    // Thread model: main thread only (writes ECS components + GPU resources).
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  GeometryPool& geometryStorage,
                  std::shared_ptr<RHI::VulkanDevice> device,
                  RHI::TransferManager& transferManager);

    // Register this system into a FrameGraph with its dependency declarations.
    // Declares: Read<Transform::WorldMatrix>, Write<MeshEdgeView::Component>,
    //           Write<MeshVertexView::Component>, WaitFor("MeshRendererLifecycle").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager);
}

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
    // Phase 6 migration: directly populates per-pass typed components
    // (Line::Component, Point::Component) from internal edge/vertex views,
    // replacing the ComponentMigration intermediary for mesh wireframe and
    // vertex visualization.
    //
    // Contract:
    //  - Auto-attaches MeshEdgeView::Component when Line::Component is
    //    present on Surface entities. Auto-detaches when Line is removed.
    //  - Auto-attaches MeshVertexView::Component when Point::Component is
    //    present on Surface entities. Auto-detaches when Point is removed.
    //
    //  - Iterates entities with MeshEdgeView + Surface.
    //    If Dirty: extracts unique edge pairs from MeshCollider collision
    //    data (triangle indices), creates an edge index buffer via
    //    ReuseVertexBuffersFrom(meshHandle), stores the handle, allocates
    //    a GPUScene slot, and clears Dirty.
    //
    //  - Populates Line::Component (Geometry, EdgeView, EdgeCount) from
    //    completed edge views every frame (idempotent).
    //
    //  - Iterates entities with MeshVertexView + Surface.
    //    If Dirty: creates a vertex view via ReuseVertexBuffersFrom(meshHandle)
    //    with Topology::Points, stores the handle, allocates a GPUScene slot,
    //    and clears Dirty.
    //
    //  - Populates Point::Component (Geometry, HasPerPointNormals) from
    //    completed vertex views every frame (idempotent).
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
    //           Write<MeshVertexView::Component>, Write<Line::Component>,
    //           Write<Point::Component>, WaitFor("MeshRendererLifecycle").
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        GeometryPool& geometryStorage,
                        std::shared_ptr<RHI::VulkanDevice> device,
                        RHI::TransferManager& transferManager);
}

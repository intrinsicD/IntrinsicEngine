module;

#include <entt/fwd.hpp>

export module Graphics:Systems.ComponentMigration;

import Core.FrameGraph;

export namespace Graphics::Systems::ComponentMigration
{
    // Transition system: attaches new per-pass typed ECS components
    // (Surface, Line, Point) alongside legacy components (MeshRenderer,
    // RenderVisualization, PointCloudRenderer) during the PLAN.md Phase 1
    // migration period.
    //
    // On each frame:
    //  - Entities with MeshRenderer::Component get Surface::Component
    //    (mirrored Geometry + Material + GpuSlot).
    //  - Entities with RenderVisualization::Component where ShowWireframe=true
    //    get Line::Component; where ShowVertices=true get Point::Component.
    //    Removal of the visualization flag removes the corresponding component.
    //  - Entities with PointCloudRenderer::Component get Point::Component
    //    (mirrored Geometry + rendering parameters).
    //  - Entities with Graph::Data get Line::Component (edges) and
    //    Point::Component (nodes) (mirrored from graph rendering parameters).
    //  - Entities with PointCloud::Data get Point::Component.
    //
    // This system is idempotent: re-running it with unchanged source
    // components produces no additional work.
    //
    // Retirement: Once Phases 2-5 of PLAN.md complete and legacy components
    // are deleted, this system is removed.
    void OnUpdate(entt::registry& registry);

    // Register this system into a FrameGraph.
    // Declares: WaitFor("TransformUpdate"), WaitFor("MeshRendererLifecycle").
    // Runs after all legacy lifecycle systems so GPU state is populated.
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry);
}

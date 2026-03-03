module;

#include <entt/fwd.hpp>

export module Graphics:Systems.ComponentMigration;

import Core.FrameGraph;

export namespace Graphics::Systems::ComponentMigration
{
    // Transition system: attaches per-pass typed ECS components (Line, Point)
    // from source components (PointCloudRenderer, Graph::Data, PointCloud::Data)
    // during the PLAN.md migration period.
    //
    // On each frame:
    //  - Entities with PointCloudRenderer::Component get Point::Component
    //    (mirrored Geometry + rendering parameters).
    //  - Entities with Graph::Data get Line::Component (edges) and
    //    Point::Component (nodes) (mirrored from graph rendering parameters).
    //  - Entities with PointCloud::Data get Point::Component.
    //
    // MeshRenderer→Surface bridging was removed: SceneManager now creates
    // Surface::Component directly. RenderVisualization→Line/Point bridging
    // was removed: Line and Point components are managed directly by
    // application code and lifecycle systems (MeshViewLifecycleSystem).
    //
    // This system is idempotent: re-running it with unchanged source
    // components produces no additional work.
    //
    // Retirement: Once remaining legacy components are deleted, this system
    // is removed.
    void OnUpdate(entt::registry& registry);

    // Register this system into a FrameGraph.
    // Declares: WaitFor("TransformUpdate"), WaitFor("MeshRendererLifecycle").
    // Runs after all legacy lifecycle systems so GPU state is populated.
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry);
}

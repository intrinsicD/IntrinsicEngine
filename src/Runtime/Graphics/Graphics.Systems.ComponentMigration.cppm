module;

#include <entt/fwd.hpp>

export module Graphics:Systems.ComponentMigration;

import Core.FrameGraph;

export namespace Graphics::Systems::ComponentMigration
{
    // Transition system: attaches Point::Component from
    // PointCloudRenderer::Component during the PLAN.md migration period.
    //
    // Phase 6 reduced scope: Graph::Data → Line+Point and
    // PointCloud::Data → Point bridging moved to their respective
    // lifecycle systems (GraphGeometrySyncSystem, PointCloudGeometrySyncSystem).
    // MeshViewLifecycleSystem directly populates Line/Point for mesh entities.
    //
    // On each frame:
    //  - Entities with PointCloudRenderer::Component get Point::Component
    //    (mirrored Geometry + rendering parameters).
    //
    // This system is idempotent: re-running it with unchanged source
    // components produces no additional work.
    //
    // Retirement: Once PointCloudRenderer::Component is deleted, this
    // system is removed.
    void OnUpdate(entt::registry& registry);

    // Register this system into a FrameGraph.
    // Declares: WaitFor("PointCloudRendererLifecycle").
    // Runs after the point cloud renderer lifecycle so GPU state is populated.
    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry);
}

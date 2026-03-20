module;

#include <entt/fwd.hpp>

export module Graphics.Systems.PrimitiveBVHSync;

import Core.FrameGraph;

export namespace Graphics::Systems::PrimitiveBVHSync
{
    // Builds local-space primitive BVHs for entities that opt in via
    // ECS::PrimitiveBVH::Data. Phase 1 supports mesh triangles, graph edges,
    // and point-cloud points as primitive domains.
    //
    // Contract:
    //  - Iterates entities with ECS::PrimitiveBVH::Data.
    //  - Chooses a source in priority order: Mesh (authoritative mesh if
    //    available, otherwise MeshCollider triangle soup), Graph edges,
    //    PointCloud points.
    //  - Rebuilds when Data::Dirty is true, when the cached BVH is missing, or
    //    when source-side dirty flags require a rebuild.
    //  - Stores a local-space Geometry::BVH plus exact primitive payload caches
    //    for consumer-side refinement.
    void OnUpdate(entt::registry& registry);

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry);
}


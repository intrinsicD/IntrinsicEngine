module;

#include <cstdint>

export module Extrinsic.ECS.Component.SpatialDebugBinding;

export namespace Extrinsic::ECS::Components
{
    // Discriminator for the geometry-tree kind referenced by a
    // SpatialDebugBinding. The enum is informational: dispatch through
    // SpatialDebugAdapterRegistry is polymorphic via ISpatialDebugAdapter,
    // so the kind exists for diagnostics and consumer filtering only.
    enum class SpatialDebugGeometryKind : std::uint8_t
    {
        Bvh        = 0,
        KdTree     = 1,
        Octree     = 2,
        ConvexHull = 3,
    };

    // Renderable↔geometry-tree binding for the runtime spatial-debug pump
    // (RUNTIME-082 Slice D). Stays plain-old-data so it remains in the ECS
    // layer alongside the other component declarations; ownership of the
    // adapter that resolves RegistryKey lives in runtime (not ECS).
    //
    // The runtime extraction pass resolves RegistryKey through
    // RenderExtractionCache's SpatialDebugAdapterRegistry; entities whose
    // key does not resolve are counted as
    // RuntimeRenderExtractionStats::SpatialDebugMissingAdapterCount and
    // contribute nothing to the snapshot batch.
    struct SpatialDebugBinding
    {
        SpatialDebugGeometryKind Kind          = SpatialDebugGeometryKind::Bvh;
        std::uint64_t            RegistryKey   = 0u;
        bool                     LeafOnly      = false;
        bool                     OccupancyOnly = false;
        std::uint32_t            MaxDepth      = 32u;
    };
}

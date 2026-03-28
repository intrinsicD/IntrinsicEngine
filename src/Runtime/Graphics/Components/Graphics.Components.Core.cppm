// =========================================================================
// Core ECS utilities — EdgePair and kInvalidGpuSlot sentinel.
// =========================================================================

module;
#include <cstdint>

export module Graphics.Components.Core;

// =========================================================================
// EdgePair — Standalone edge index pair type.
// =========================================================================
// Two uint32 vertex indices referencing into a position array.
// Used by Graph::Data::CachedEdgePairs, LinePass BDA uploads,
// MeshViewLifecycleSystem, and GraphLifecycleSystem.

export namespace ECS
{
    struct EdgePair
    {
        uint32_t i0;
        uint32_t i1;
    };
    static_assert(sizeof(EdgePair) == 8);

    // Sentinel value for unallocated GPUScene slots.
    // Shared by all component types that hold a GpuSlot member.
    inline constexpr uint32_t kInvalidGpuSlot = ~0u;
}

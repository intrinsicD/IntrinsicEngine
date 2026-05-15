module;

#include <cstdint>

#include <entt/fwd.hpp>

export module Extrinsic.ECS.System.RenderSync;

import Extrinsic.Core.FrameGraph;

export namespace Extrinsic::ECS::Systems::RenderSync
{
    // Pass identifier used by the promoted FrameGraph registration helper and
    // by downstream consumers that need to declare an ordering edge against
    // the render-sync hand-off.
    inline constexpr const char* PassName = "RenderSync";

    // Optional CPU-only diagnostics counters. The system never logs or
    // throws; consumers pass a `Stats&` if they want per-frame visibility
    // into how many entities the tag-forwarding pass touched.
    struct Stats
    {
        // Number of entities that carried `Transform::WorldUpdatedTag` on
        // entry to the pass (i.e. updates produced by the transform
        // hierarchy this substep).
        std::uint32_t WorldUpdatedObserved = 0;
        // Number of entities on which `DirtyTags::DirtyTransform` was
        // newly stamped (or refreshed via emplace_or_replace) by the pass.
        std::uint32_t DirtyTransformStamped = 0;
        // Number of entities on which `Transform::WorldUpdatedTag` was
        // cleared by the pass.
        std::uint32_t WorldUpdatedCleared = 0;
    };

    // Forward the CPU "world matrix was rewritten this substep" signal
    // (`Transform::WorldUpdatedTag`, produced by
    // `Systems::TransformHierarchy`) into the GPU-sync signal
    // (`DirtyTags::DirtyTransform`, drained by runtime render extraction)
    // and clear `WorldUpdatedTag` so the producer/consumer cycle is
    // closed within the ECS layer.
    //
    // This pass is intentionally CPU-only and does not touch any
    // graphics/RHI/runtime sidecars: it is a pure tag-forwarding seam
    // sitting between transform propagation (and bounds propagation,
    // which also reads `WorldUpdatedTag`) and the runtime render
    // extraction lane that consumes `DirtyTransform`.
    void OnUpdate(entt::registry& registry);
    void OnUpdate(entt::registry& registry, Stats& stats);

    // Register the forwarding pass with declared dependencies. The pass
    // waits for `TransformHierarchy::PassName` (so it observes freshly
    // emitted `WorldUpdatedTag` entries) and for
    // `BoundsPropagation::PassName` (so that bounds propagation has
    // already read `WorldUpdatedTag` before this pass clears it). It
    // signals `RenderSync::PassName` for any downstream pass that wants
    // to wait for the GPU-sync hand-off to be in place.
    void RegisterSystem(Extrinsic::Core::FrameGraph& graph, entt::registry& registry);
}

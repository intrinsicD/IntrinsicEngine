module;

#include <cstdint>

#include <entt/fwd.hpp>
#include <glm/glm.hpp>

export module Extrinsic.ECS.System.BoundsPropagation;

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;

export namespace Extrinsic::ECS::Systems::BoundsPropagation
{
    // Pass identifier used by the promoted FrameGraph registration helper and
    // by downstream systems that need to declare an ordering edge against the
    // bounds propagation pass.
    inline constexpr const char* PassName = "WorldBoundsUpdate";

    // Optional diagnostics counters. The system never logs or throws; consumers
    // pass a Stats& if they want per-frame visibility into recompute coverage
    // and into entities skipped because they were missing required inputs or
    // produced non-finite world bounds.
    struct Stats
    {
        std::uint32_t Recomputed = 0;
        std::uint32_t SkippedMissingLocalBounds = 0;
        std::uint32_t SkippedMissingWorldMatrix = 0;
        std::uint32_t NonFiniteResults = 0;
    };

    // Pure initialisation seam for runtime-owned composition paths that must
    // publish usable world bounds before the next frame-graph traversal (for
    // example import completion and camera framing). Returns false and leaves
    // `outWorld` unchanged when the transform would produce non-finite bounds.
    [[nodiscard]] bool TryComputeWorldBounds(
        const Components::Culling::Local::Bounds& local,
        const glm::mat4& worldMatrix,
        Components::Culling::World::Bounds& outWorld) noexcept;

    // Recompute Components::Culling::World::Bounds from
    // Components::Culling::Local::Bounds and Components::Transform::WorldMatrix
    // for every entity that the transform hierarchy stamped with
    // Components::Transform::WorldUpdatedTag this frame. The world OBB inherits
    // the rotation embedded in the world matrix and scales its extents per
    // matrix column. When a valid local AABB is available, the world sphere
    // conservatively encloses its transformed corners, including affine shear
    // produced by composed non-uniform scale and rotation. Sphere-only inputs
    // use the largest column magnitude as their scale fallback. World bounds
    // are written via emplace_or_replace so first-frame entities and entities
    // whose local bounds were freshly authored receive an initial world value.
    //
    // The WorldUpdatedTag is NOT cleared here; render-sync owns that hand-off.
    void OnUpdate(entt::registry& registry);
    void OnUpdate(entt::registry& registry, Stats& stats);

    // Register the propagation as a FrameGraph pass with declared dependencies
    // and a WaitFor edge against TransformHierarchy::PassName so propagation
    // observes freshly-written world matrices.
    void RegisterSystem(Extrinsic::Core::FrameGraph& graph, entt::registry& registry);
}

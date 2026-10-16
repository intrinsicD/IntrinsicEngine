// Cached transform propagation and checked composition of current authored parent chains.
module;

#include <optional>
#include <entt/fwd.hpp>
#include <glm/mat4x4.hpp>

export module Extrinsic.ECS.System.TransformHierarchy;

import Extrinsic.Core.FrameGraph;

export namespace Extrinsic::ECS::Systems::TransformHierarchy
{
    // Compose current local transforms without reading or updating WorldMatrix.
    // Missing local components contribute identity; entt::null is the identity
    // root. Invalid parents, cycles, chains over 4096 nodes and nonfinite results
    // return nullopt. No hierarchy or dirty/cache components are required.
    [[nodiscard]] std::optional<glm::mat4> TryComposeWorldMatrix(
        const entt::registry& registry, entt::entity entity);

    // Pass identifier used by the promoted FrameGraph registration helper and
    // by downstream systems that need to declare an ordering edge against the
    // transform update.
    inline constexpr const char* PassName = "TransformUpdate";

    // Walk every root entity (no Hierarchy parent) and recompute the world
    // matrix for entities whose local transform is dirty (or whose ancestor
    // is dirty). On entities that get rewritten:
    //   - emplace Components::Transform::WorldUpdatedTag (consumer-cleared);
    //   - remove Components::Transform::IsDirtyTag (this system's contract).
    // GPU-sync (Components::DirtyTags::DirtyTransform) is not stamped here;
    // render-sync owns that hand-off.
    void OnUpdate(entt::registry& registry);

    // Register the traversal as a FrameGraph pass with declared dependencies:
    //   Read<Transform::Component>, Read<Hierarchy::Component>,
    //   Write<Transform::WorldMatrix>, Write<Transform::IsDirtyTag>,
    //   Write<Transform::WorldUpdatedTag>, Signal("TransformUpdate").
    // The promoted simulate-phase bundle activation lands in HARDEN-061 Slice 2.
    void RegisterSystem(Extrinsic::Core::FrameGraph& graph, entt::registry& registry);
}

module;

#include <entt/entity/registry.hpp>

export module Extrinsic.ECS.Hierarchy.Mutation;

import Extrinsic.ECS.Scene.Handle;

export namespace Extrinsic::ECS::Hierarchy
{
    // Reparent `child` under `newParent`, preserving world-space pose when
    // both endpoints have ready Transform/WorldMatrix state. Behaviour:
    //   - invalid child or self-parent: no-op.
    //   - newParent == InvalidEntityHandle: equivalent to Detach(registry, child).
    //   - cycle (newParent is a descendant of child): no-op.
    //   - already attached to newParent: no-op.
    //   - reparent: detach from old parent, recompute child local transform
    //     so that world matrix is preserved, mark child IsDirtyTag, attach.
    //   - if the parent's world matrix is singular (non-invertible) the child
    //     local TRS resets to identity and IsDirtyTag is set.
    void Attach(entt::registry& registry, EntityHandle child, EntityHandle newParent);

    // Detach `child` from its current parent, leaving it as a root entity.
    // No-op if invalid or already detached.
    void Detach(entt::registry& registry, EntityHandle child);
}

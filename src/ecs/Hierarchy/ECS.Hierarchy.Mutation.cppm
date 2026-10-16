// Declares hierarchy mutations that preserve world-space pose during reparenting.
module;

#include <entt/entity/fwd.hpp>

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
    //   - if the finite parent world matrix is singular, child local resets to
    //     identity and IsDirtyTag is set. Nonfinite matrices or unrepresentable
    //     local TRS otherwise reject before changing links or the local pose.
    void Attach(entt::registry& registry, EntityHandle child, EntityHandle newParent);

    // Detach `child` from its current parent, leaving it as a root entity.
    // No-op if invalid or already detached; otherwise always unlinks. Ready
    // world state supplies root-local TRS with exact translation; shear is lost.
    // Unready/non-decomposable world state keeps authored local. A local transform
    // is marked dirty in either case so teardown loops always make progress.
    void Detach(entt::registry& registry, EntityHandle child);
}

# ECS/Systems

This directory contains the `Systems` module/files.

## Contents

- `CMakeLists.txt`
- `ECS.System.BoundsPropagation.cpp`
- `ECS.System.BoundsPropagation.cppm`
- `ECS.System.RenderSync.cpp`
- `ECS.System.RenderSync.cppm`
- `ECS.System.TransformHierarchy.cpp`
- `ECS.System.TransformHierarchy.cppm`

## Transform hierarchy traversal

`Extrinsic.ECS.System.TransformHierarchy::OnUpdate(entt::registry&)` walks
every root entity (Hierarchy parent == InvalidEntityHandle) and recomputes
the world matrix for any subtree where either the entity's local transform
is dirty (`Components::Transform::IsDirtyTag`) or an ancestor was rewritten
on the same pass. Entities whose world matrix the system rewrites get
`Components::Transform::WorldUpdatedTag`; the CPU dirty tag is cleared on
those entities. The promoted CPU traversal does **not** stamp
`Components::DirtyTags::DirtyTransform` — that GPU-sync hand-off remains a
render-sync responsibility.

`RegisterSystem(FrameGraph&, registry&)` adds the traversal as a FrameGraph
pass named `"TransformUpdate"` declaring `Read<Transform::Component>`,
`Read<Hierarchy::Component>`, `Write<Transform::WorldMatrix>`,
`Write<Transform::IsDirtyTag>`, `Write<Transform::WorldUpdatedTag>`, and
`Signal("TransformUpdate")`. The runtime activates this pass every fixed-step
substep through `Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle`
(`RUNTIME-091`), invoked after `IApplication::OnSimTick` and before
`Core::FrameGraph::Compile` so app passes that mutate transforms run before
the traversal and passes that `WaitFor("TransformUpdate")` run after it.

## World bounds propagation

`Extrinsic.ECS.System.BoundsPropagation::OnUpdate(entt::registry&)` recomputes
`Components::Culling::World::Bounds` from `Components::Culling::Local::Bounds`
and `Components::Transform::WorldMatrix` for every entity that
`TransformHierarchy` stamped with `Components::Transform::WorldUpdatedTag`
this frame.

Selected propagation policy: **driven by `WorldUpdatedTag`**. The system does
not introduce a separate local-bounds dirty tag and does not perform full
scans. Producers that mutate a local AABB/sphere in isolation must therefore
also mark the owning transform dirty (so the hierarchy traversal stamps
`WorldUpdatedTag`) if they need the world bounds refreshed on the same
frame; this keeps propagation O(updated subtrees) rather than O(N).

The world OBB inherits the rotation embedded in the world matrix; world AABB
extents are scaled per-column; the world sphere uses the largest column
magnitude as a conservative scale factor. Writes use `emplace_or_replace`
so first-frame entities and entities whose local bounds were freshly
authored receive an initial world value. The `WorldUpdatedTag` is **not**
cleared here — render-sync owns that hand-off.

An overload `OnUpdate(registry&, Stats&)` accumulates CPU-only diagnostics
(`Recomputed`, `SkippedMissingLocalBounds`, `SkippedMissingWorldMatrix`,
`NonFiniteResults`). The system never logs or throws; non-finite outputs are
counted and the entity's existing world bounds are left untouched.

`RegisterSystem(FrameGraph&, registry&)` registers the pass named
`"WorldBoundsUpdate"`, with `WaitFor("TransformUpdate")`,
`Read<Culling::Local::Bounds>`, `Read<Transform::WorldMatrix>`,
`Read<Transform::WorldUpdatedTag>`, `Write<Culling::World::Bounds>`, and
`Signal("WorldBoundsUpdate")`. The runtime activates this pass alongside
`TransformHierarchy` through `Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle`
(`RUNTIME-091`) so world bounds refresh on the same substep that recomputes
the world matrix.

## Render sync boundary

Per
[`GRAPHICS-028`](../../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md),
GPU-handle-touching render residency does not belong in ECS systems.
Per [`HARDEN-066`](../../../tasks/done/HARDEN-066-ecs-render-sync-export-policy.md),
`Extrinsic.ECS.System.RenderSync` is a CPU-only tag-forwarding pass that
translates `Components::Transform::WorldUpdatedTag` (the producer signal
emitted by `TransformHierarchy`) into `Components::DirtyTags::DirtyTransform`
(the GPU-sync hand-off drained by `Runtime.RenderExtraction`). It also
clears `WorldUpdatedTag` so the producer/consumer cycle is closed within
the ECS layer; downstream consumers that need a "transform changed" signal
read `DirtyTransform` instead of subscribing to `WorldUpdatedTag` directly.

`Extrinsic.ECS.Systems.RenderSync::OnUpdate(registry)` iterates entities
carrying `WorldUpdatedTag`, stamps `DirtyTransform` via
`emplace_or_replace`, and bulk-clears `WorldUpdatedTag` afterward. The
overload `OnUpdate(registry, Stats&)` accumulates CPU-only diagnostics
(`WorldUpdatedObserved`, `DirtyTransformStamped`, `WorldUpdatedCleared`)
without logging or throwing.

`RegisterSystem(FrameGraph&, registry&)` registers the pass named
`"RenderSync"` with `WaitFor("TransformUpdate")`,
`WaitFor("WorldBoundsUpdate")`, `Write<Transform::WorldUpdatedTag>`,
`Write<DirtyTags::DirtyTransform>`, and `Signal("RenderSync")`. The two
`WaitFor` edges guarantee `BoundsPropagation` reads `WorldUpdatedTag`
before this pass clears it. The runtime activates this pass alongside
`TransformHierarchy` and `BoundsPropagation` through
`Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle`
(`RUNTIME-091`) so the `DirtyTransform` hand-off lands every fixed-step
substep.

Calls to `GpuWorld`, `GpuAssetCache`, RHI managers, or graphics-owned
`GpuSceneSlot` storage remain runtime responsibilities owned by
`Runtime.RenderExtraction` or a runtime residency sibling.

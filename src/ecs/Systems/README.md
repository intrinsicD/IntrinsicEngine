# ECS/Systems

This directory contains the `Systems` module/files.

## Contents

- `CMakeLists.txt`
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
`Signal("TransformUpdate")`. The promoted simulate-phase bundle activation
of this helper is tracked in `tasks/active/HARDEN-061-*` Slice 2.

## Render sync boundary

Per
[`GRAPHICS-028`](../../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md),
GPU-handle-touching render residency does not belong in ECS systems.
`ECS.System.RenderSync` may remain a CPU-only aggregation or forwarding seam if
needed, but calls to `GpuWorld`, `GpuAssetCache`, RHI managers, or graphics-owned
`GpuSceneSlot` storage are runtime responsibilities owned by
`Runtime.RenderExtraction` or a runtime residency sibling.

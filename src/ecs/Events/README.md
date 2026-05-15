# ECS/Events

CPU-only event payload types fired by promoted ECS mutations or
geometry operators. Consumers (runtime, editor, undo stack, view
re-sync) subscribe through their own dispatchers; this module defines
only the value shapes.

## Public module surface

- `Extrinsic.ECS.Events`

## Contents

- `CMakeLists.txt`
- `ECS.Events.cppm`

## Ownership policy (HARDEN-063)

The promoted ECS layer owns CPU-only event payload types that describe
scene-side mutations. It does **not** own dispatch, queueing,
subscription, or input interpretation — those belong in
`runtime`/`editor` per the layering contract in
[`/AGENTS.md`](../../../AGENTS.md) §2.

Promoted events (this module):

- `SelectionChanged` — fired after Components::Selection::SelectedTag
  membership changes.
- `HoverChanged` — fired after Components::Selection::HoveredTag changes.
- `EntitySpawned` — fired after a promoted creation path adds an entity.
- `GeometryModified` — fired after a CPU geometry operator mutates
  mesh/graph/point-cloud data on an entity.

Legacy events that did **not** promote to ECS:

- `GpuPickCompleted` — owned by `runtime`/`graphics`. GPU pick readback
  is graphics-owned; the ECS layer never sees `PickID`, primitive IDs,
  or readback completion. Runtime/editor translates a completed pick
  into a selection mutation, which then fires the promoted
  `SelectionChanged` event.
- `GeometryUploadFailed` — owned by `runtime`/`graphics`. GPU upload
  failure is a graphics-layer concern; the ECS layer learns about the
  failure only through the corresponding asset-event or runtime
  diagnostics.

## Layering

Events import only `Extrinsic.ECS.Scene.Handle` from the same layer.
Adding new event payloads requires the same CPU-only constraint: no
imports of `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`,
`Extrinsic.Runtime.*`, `Extrinsic.Platform.*`, `Extrinsic.App.*`, or
`Extrinsic.Asset.*` modules. The contract test
[`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp`](../../../tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp)
catches accidental upward imports.

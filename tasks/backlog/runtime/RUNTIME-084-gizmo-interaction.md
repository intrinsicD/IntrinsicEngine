# RUNTIME-084 — `Extrinsic.Runtime.GizmoInteraction` umbrella

## Goal
- Open the runtime/editor-side gizmo interaction umbrella declared by `GRAPHICS-017Q`: a new module `Extrinsic.Runtime.GizmoInteraction` (planned home: `src/runtime/Gizmos/Runtime.GizmoInteraction.cppm`) that owns transform-gizmo hit testing, interaction state (mode, axis lock, drag origin, snap, modifier keys, multi-select pivot, orientation reference frame), authoring-transform application, and undo/redo command emission. Hit testing reads `CameraViewSnapshot::ViewProjection` / `PickRay` and platform pointer pixels; graphics receives only render-relevant `TransformGizmoRenderPacket` data.

## Non-goals
- No graphics-side hit testing or interaction state.
- No mutation of `TransformGizmoRenderPacket` field set (frozen by `GRAPHICS-017Q`).
- No editor UI panels (consumer of this module).
- No camera-controller code (`RUNTIME-081`).

## Context
- Status: not started.
- Owner/layer: `runtime` (and editor — for now keep it under runtime; editor-specific surfaces can move later).
- Planning anchor: `tasks/done/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md` ("Transform-gizmo hit testing is runtime/editor-owned under the planned umbrella module name `Extrinsic.Runtime.GizmoInteraction`").
- Today: `TransformGizmoRenderPacket` exists on `RuntimeRenderSnapshotBatch::TransformGizmos` and the renderer accepts the spans. No runtime-side hit-test or interaction state exists; the spans never carry data.
- Per `GRAPHICS-017Q`: transform application writes ECS authoring transforms during drag-tick; drag-commit pushes a single undoable `(entity, before, after)` command onto the editor undo stack. Undo/redo lives entirely in the editor; graphics never mutates ECS, asset, or prefab state.

## Required changes
- [ ] Add `src/runtime/Gizmos/Runtime.GizmoInteraction.cppm` exporting `Extrinsic.Runtime.GizmoInteraction` with:
  - `class GizmoInteraction { … }` owning per-frame interaction state (`Mode`, `AxisLock`, `DragOrigin`, `SnapStep`, `ModifierMask`, `MultiSelectPivot`, `OrientationFrame`),
  - hit-test API `(CameraViewSnapshot, PickRay, std::span<EntityId>) -> HitResult`,
  - drag-tick API mutating runtime ECS authoring transforms,
  - drag-commit API emitting `(entity, before, after)` records to a runtime-owned `UndoStack` (or an editor-provided callback).
- [ ] Add a runtime-owned `TransformGizmoRenderPacketBuilder` that produces `TransformGizmoRenderPacket` records for the active interaction state, populating `RuntimeRenderSnapshotBatch::TransformGizmos`.
- [ ] Wire `Engine::OnVariableTick` to call `GizmoInteraction::Tick(input, cameraSnapshot, selection)` and refresh the render-packet span before snapshot submission.

## Tests
- [ ] `contract;runtime` test: hit-test against a translate-axis-X gizmo at a known pose returns the X-axis as the highlighted handle for a ray that passes through it; returns no-hit for a ray that misses by 2 px.
- [ ] `contract;runtime` test: drag-tick on the X-axis updates `Transform::Component::Translation.x` by the projected delta; on drag-commit, the undo stack receives one record with the correct before/after values.
- [ ] `contract;runtime` test: snap-step rounding works as expected when the modifier is held.
- [ ] `contract;runtime` test: `TransformGizmoRenderPacket` produced by the builder carries only the recorded fields (no drag state, axis lock, snap thresholds, or modifier keys).
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `GizmoInteraction` umbrella row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] Hit-test, drag-tick, drag-commit, and undo emission produce deterministic results.
- [ ] `TransformGizmoRenderPacket` field set is preserved exactly; no graphics-visible interaction state leaks.
- [ ] No graphics imports beyond `Graphics.CameraSnapshots` (existing edge) and `Graphics.RenderWorld` (gizmo packet types).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mutating ECS state from graphics.
- Adding interaction state fields to `TransformGizmoRenderPacket`.
- Reading raw pointer coordinates from graphics.

## Next verification step
- Land the module + per-axis hit-test + drag-tick + undo emission, exercise the contract tests above.

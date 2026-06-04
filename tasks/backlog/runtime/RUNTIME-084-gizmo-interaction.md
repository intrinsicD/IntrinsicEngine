# RUNTIME-084 — `Extrinsic.Runtime.GizmoInteraction` umbrella

## Goal
- Open the runtime/editor-side gizmo interaction umbrella declared by `GRAPHICS-017Q`: a new module `Extrinsic.Runtime.GizmoInteraction` (planned home: `src/runtime/Gizmos/Runtime.GizmoInteraction.cppm`) that owns transform-gizmo hit testing, interaction state (mode, axis lock, drag origin, snap, modifier keys, multi-select pivot, orientation reference frame), authoring-transform application, and undo/redo command emission. Hit testing reads `CameraViewSnapshot::ViewProjection` / `PickRay` and platform pointer pixels; graphics receives only render-relevant `TransformGizmoRenderPacket` data.

## Non-goals
- No graphics-side hit testing or interaction state.
- No mutation of `TransformGizmoRenderPacket` field set (frozen by `GRAPHICS-017Q`).
- No editor UI panels (consumer of this module).
- No camera-controller code (`RUNTIME-081`).

## Context
- Status: Slice A landed (standalone module + `contract;runtime` coverage,
  `CPUContracted`); Slice B (`Engine`/extraction wiring + input binding) remains.
- Owner/layer: `runtime` (and editor — for now keep it under runtime; editor-specific surfaces can move later).

## Slice plan
- **Slice A (landed 2026-06-03).** Standalone `Extrinsic.Runtime.GizmoInteraction`
  module + `TransformGizmoRenderPacketBuilder` + `GizmoUndoStack`: screen-space
  axis-handle hit testing, axis-constrained translate drag against ECS authoring
  transforms, snap rounding, undo emission, and the frozen render-packet field
  mapping, all behind pure `contract;runtime` tests. Preserves the default CPU
  gate. Defers `Engine` wiring + real pointer-input binding to Slice B. Rotate/
  Scale drag *application* is also Slice B, so Slice A's `BeginDrag`/`DragTick`
  reject non-`Translate` modes (no-op) rather than move entities through the
  translate path. Maturity `CPUContracted`.
- **Slice B (deferred).** Wire `Engine::OnVariableTick` to build the gizmo pivot
  camera snapshot from the active camera controller, read the window cursor +
  button state, drive `HitTest`/`BeginDrag`/`DragTick`/`DragCommit`, and refresh
  the `RuntimeRenderSnapshotBatch::TransformGizmos` span before snapshot
  submission. The concrete mouse-button/modifier → drag-intent binding is an
  editor/UI policy decision (mirroring the `RUNTIME-089` Slice B deferral of the
  input→pick binding), so Slice B composes with the editor input task. This is
  the `Operational` follow-up; rotate/scale drag application also lands here.
- Planning anchor: `tasks/done/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md` ("Transform-gizmo hit testing is runtime/editor-owned under the planned umbrella module name `Extrinsic.Runtime.GizmoInteraction`").
- Today: `TransformGizmoRenderPacket` exists on `RuntimeRenderSnapshotBatch::TransformGizmos` and the renderer accepts the spans. No runtime-side hit-test or interaction state exists; the spans never carry data.
- Per `GRAPHICS-017Q`: transform application writes ECS authoring transforms during drag-tick; drag-commit pushes a single undoable `(entity, before, after)` command onto the editor undo stack. Undo/redo lives entirely in the editor; graphics never mutates ECS, asset, or prefab state.

## Required changes
- [x] Add `src/runtime/Gizmos/Runtime.GizmoInteraction.cppm` exporting `Extrinsic.Runtime.GizmoInteraction` with (Slice A):
  - `class GizmoInteraction { … }` owning per-frame interaction state (`Mode`, `AxisLock`, `DragOrigin`, snap step via `GizmoConfig`, `ModifierMask`, multi-select pivot, `Orientation` frame),
  - hit-test API `HitTest(Registry, CameraViewSnapshot, cursorPixel, viewport, span<EntityHandle>) -> GizmoHitResult` (screen-space handle pick; `PickRay` is the explicit world-ray type used by drag),
  - `BeginDrag`/`DragTick` API mutating runtime ECS authoring transforms (`Transform::Component::Position`),
  - `DragCommit` API emitting `(entity, before, after)` records to a runtime-owned `GizmoUndoStack`.
- [x] Add a runtime-owned `TransformGizmoRenderPacketBuilder` that produces `TransformGizmoRenderPacket` records for the active interaction state (Slice A). _(Populating `RuntimeRenderSnapshotBatch::TransformGizmos` from the builder is the Slice B extraction wiring.)_
- [ ] **(Slice B)** Wire `Engine::OnVariableTick` to call the interaction with the camera snapshot, cursor input, and selection, and refresh the render-packet span before snapshot submission.

## Tests
- [x] `contract;runtime` test: hit-test against a translate-axis-X gizmo at a known pose returns the X-axis as the highlighted handle for a cursor on the projected handle; returns no-hit for a cursor off every handle (`HitTestResolvesXAxisAndRejectsOffAxisCursor`).
- [x] `contract;runtime` test: drag-tick on the X-axis updates `Transform::Component::Position.x` by the projected axis delta; on drag-commit, the undo stack receives one record with the correct before/after values (`DragTickTranslatesAlongAxisAndCommitEmitsUndoRecord`).
- [x] `contract;runtime` test: snap-step rounding works as expected when the modifier is held (`SnapModifierRoundsTranslationToStep`).
- [x] `contract;runtime` test: `TransformGizmoRenderPacket` produced by the builder carries only the recorded fields (no drag state, axis lock, snap thresholds, or modifier keys) (`RenderPacketBuilderMapsOnlyFrozenFields`).
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `GizmoInteraction` umbrella row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [x] Hit-test, drag-tick, drag-commit, and undo emission produce deterministic results (Slice A contract tests).
- [x] `TransformGizmoRenderPacket` field set is preserved exactly; no graphics-visible interaction state leaks (Slice A `RenderPacketBuilderMapsOnlyFrozenFields`).
- [x] No graphics imports beyond `Graphics.CameraSnapshots` (existing edge) and `Graphics.RenderWorld` (gizmo packet types).

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
- Slice A landed and verified (`IntrinsicRuntimeContractTests`, labels `contract;runtime`).
- Slice B: wire `Engine::OnVariableTick`/extraction and the editor input→drag
  binding, then add an `integration;runtime` (or editor) wiring test that drives a
  bounded frame and asserts the `TransformGizmos` span is populated for the
  active selection.

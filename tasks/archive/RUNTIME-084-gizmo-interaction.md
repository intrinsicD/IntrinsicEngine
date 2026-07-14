# RUNTIME-084 — `Extrinsic.Runtime.GizmoInteraction` umbrella

## Status
- State: `done` — retired 2026-06-06 at maturity `CPUContracted` for the runtime/editor data-only handoff.
- Owner/agent: codex.
- Branch: `main`.
- PR/commit: pending commit in this session.
- Maturity reached: `CPUContracted`. Slice A landed the standalone `Extrinsic.Runtime.GizmoInteraction` module, packet builder, undo stack, and deterministic hit-test/translate contract tests. Slice B wires the runtime frame path: `Engine` owns gizmo interaction state and undo storage, reads platform input plus the frame camera, applies translate/rotate/scale edits to ECS authoring transforms, builds `TransformGizmoRenderPacket` records for selected entities, and passes those records through `RenderExtractionCache::ExtractAndSubmit` into `RuntimeRenderSnapshotBatch::TransformGizmos`.
- `Operational` Vulkan/CUDA follow-up: none owed by this runtime task. Graphics remains data-only for gizmos and receives copied `TransformGizmoRenderPacket` values only; visual presentation and broader legacy-editor parity stay tracked by graphics/editor retirement follow-ups.

## Goal
Open the runtime/editor-side gizmo interaction umbrella declared by `GRAPHICS-017Q`: `Extrinsic.Runtime.GizmoInteraction` owns transform-gizmo hit testing, interaction state, authoring-transform application, render-packet production, and undo/redo command emission. Hit testing reads `CameraViewSnapshot::ViewProjection` / `PickRay` and platform pointer pixels; graphics receives only render-relevant `TransformGizmoRenderPacket` data.

## Non-goals
- No graphics-side hit testing or interaction state.
- No mutation of `TransformGizmoRenderPacket` field set (frozen by `GRAPHICS-017Q`).
- No editor UI panels.
- No camera-controller code (`RUNTIME-081`).
- No Vulkan/CUDA backend work; this task is the runtime producer handoff.

## Context
- Owner/layer: `runtime` (and editor-facing policy surfaces). Graphics consumes immutable snapshot data only.
- Planning anchor: `tasks/archive/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md` assigned transform-gizmo hit testing and transform application to runtime/editor under `Extrinsic.Runtime.GizmoInteraction`.
- Slice A landed on 2026-06-03 at `CPUContracted`: standalone module + `TransformGizmoRenderPacketBuilder` + `GizmoUndoStack` with screen-space axis-handle hit testing, translate drag, snap rounding, undo emission, and frozen render-packet mapping.
- Slice B landed on 2026-06-06: rotate/scale drag application, full transform undo tuples, mode latching, cancel restore for position/rotation/scale, `Engine` input/camera/selection wiring, and `RenderExtractionCache` submission of `TransformGizmos`.
- The default binding is intentionally small: left mouse begins/ticks/commits a gizmo drag and left shift maps to `GizmoModifier::Snap`. Rich editor policy such as numeric input, multi-select pivot variants, additional modifiers, and panel UX remains outside this runtime handoff.

## Required changes
- [x] Add `src/runtime/Gizmos/Runtime.GizmoInteraction.cppm` exporting `Extrinsic.Runtime.GizmoInteraction` with `GizmoInteraction`, per-frame interaction state, hit testing, drag lifecycle, diagnostics, and `GizmoUndoStack`.
- [x] Add `TransformGizmoRenderPacketBuilder` that produces only `Graphics::TransformGizmoRenderPacket` records from selected runtime entities.
- [x] Apply translate, rotate, and scale drags along the locked axis; latch operation mode at `BeginDrag` so toolbar changes do not reinterpret an active drag.
- [x] Emit undo records carrying before/after position, rotation, and scale, and restore the same tuple on drag cancel.
- [x] Wire `Engine` to own `GizmoInteraction`, `GizmoUndoStack`, the packet builder, and selected gizmo entity storage.
- [x] Wire the per-frame runtime path to build a camera pick ray from the active frame camera, read pointer/button/modifier input from the platform window, drive hit-test/drag tick/commit, and build transform-gizmo packets from the active selection.
- [x] Extend `RenderExtractionCache::ExtractAndSubmit` to accept a transform-gizmo packet span and submit it through `RuntimeRenderSnapshotBatch::TransformGizmos`.

## Tests
- [x] `contract;runtime` test: hit-test against a translate-axis-X gizmo at a known pose returns the X-axis and rejects off-handle cursors (`HitTestResolvesXAxisAndRejectsOffAxisCursor`).
- [x] `contract;runtime` test: empty selections return no-hit (`HitTestEmptySelectionIsNoHit`).
- [x] `contract;runtime` test: translate drag updates `Transform::Component::Position.x` and emits one undo record (`DragTickTranslatesAlongAxisAndCommitEmitsUndoRecord`).
- [x] `contract;runtime` test: rotate drag updates `Transform::Component::Rotation` and emits one undo record (`DragTickRotatesAroundAxisAndCommitEmitsUndoRecord`).
- [x] `contract;runtime` test: scale drag updates only the locked axis scale and emits one undo record (`DragTickScalesAlongAxisAndCommitEmitsUndoRecord`).
- [x] `contract;runtime` test: drag cancel restores position, rotation, and scale (`DragCancelRestoresBeforeTransform`).
- [x] `contract;runtime` test: snap-step rounding works when the modifier is held (`SnapModifierRoundsTranslationToStep`).
- [x] `contract;runtime` test: active drag mode is latched across toolbar mode changes (`DragModeIsLatchedWhenToolbarModeChangesMidDrag`).
- [x] `contract;runtime` test: render-packet builder maps only the frozen graphics-visible field set (`RenderPacketBuilderMapsOnlyFrozenFields`).
- [x] `contract;runtime` test: direct extraction submits transform-gizmo packets into `RenderWorld::Gizmos` (`ExtractionSubmitsTransformGizmoPackets`).
- [x] `contract;runtime` test: bounded `Engine::Run()` publishes a selected entity's transform-gizmo packet through the runtime frame path (`RunFramePublishesSelectedEntityGizmoPacket`).
- [x] No `gpu`/`vulkan`/`cuda` test is required for this runtime-only data handoff.

## Docs
- [x] Update `src/runtime/README.md` to describe the concrete `GizmoInteraction` state and Engine/extraction wiring.
- [x] Update ADR/rendering backlog references that still described `Extrinsic.Runtime.GizmoInteraction` as only planned.
- [x] Refresh `docs/api/generated/module_inventory.md` after public module-surface changes.
- [x] Update backlog indexes to point `RUNTIME-084` at `tasks/done/`.

## Acceptance criteria
- [x] Hit-test, drag-tick, drag-commit, drag-cancel, and undo emission produce deterministic CPU/default-gate results.
- [x] Translate, rotate, and scale edits mutate runtime ECS authoring transforms only in runtime/editor code.
- [x] `TransformGizmoRenderPacket` field set is preserved exactly; no drag state, snap thresholds, modifier keys, pointer pixels, undo state, ECS handles, or platform input leak into graphics.
- [x] `Engine` owns the gizmo interaction state and submits transform-gizmo packet spans before renderer snapshot extraction.
- [x] Graphics imports no runtime/editor/platform input code and mutates no ECS state for gizmo interaction.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'GizmoInteraction|GizmoInteractionEngineWiring' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
ctest --test-dir build/ci --output-on-failure -L 'contract' -R 'Runtime|Gizmo|RenderExtraction|Selection' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
tools/ci/run_clean_workshop_review.sh . --strict
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

## Forbidden changes
- Mutating ECS state from graphics.
- Adding interaction state fields to `TransformGizmoRenderPacket`.
- Reading raw pointer coordinates from graphics.
- Adding Vulkan/CUDA backend dependencies to runtime gizmo interaction.

---
id: UI-078
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive feature work; evidence is the diff, CPU contract tests, the ImGui integration suite, a Vulkan acceptance smoke, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, repo.task-contract-discovery, runtime.gizmo-transform-session]
contract_review: Reviewed the catalog. The change alters module surfaces and READMEs (source documentation) and the reusable runtime/UI gizmo interaction contract (task contract discovery); slice 1 adds `runtime.gizmo-transform-session` (source ADR 0006 amendment, proofs the CPU contract tests) for the session rules slices 2–3 and other transform-preview users build on; slice 2 extends that contract (no parallel one) with viewport input ownership and the lifecycle cancel triggers. Engine/kernel/editor-frame locality contracts do not apply because those surfaces and dependencies are not extended; no method or geometry-property contract applies to entity TRS.
---
# UI-078 — Edit entity transforms with an ImGuizmo gizmo

## Goal
- Edit the transform of selected entities interactively with ImGuizmo
  (translate, rotate, scale). Origin: REVIEW-007 T21 — the operator kept the
  requested-but-unlinked `imguizmo` dependency for this feature (2026-10-06).
- The gizmo is enabled only through the UI. While it is active, dragging it
  previews the transform; releasing records one undoable command.
- With several selected entities, all of them transform together around their
  common pivot during the interaction.

## Acceptance criteria
- [ ] Gizmo is off by default and can only be switched on from the editor UI
      (menu/toolbar); translate/rotate/scale modes are selectable there.
- [ ] Multi-selection rotates/scales/translates around one pivot frozen at drag
      start; positions move around the pivot, not only per-entity rotation/scale.
- [ ] A UI toggle selects the pivot: mean of selected world origins (default) or
      mean of world-bounds centers (entities without bounds fall back to their
      origin).
- [ ] Local/global orientation works for single entities and groups.
- [ ] W/E/R switch translate/rotate/scale while the gizmo is active (not while a
      text field has focus); Escape cancels the drag.
- [ ] Snap steps are configurable in the UI (defaults 0.25, 15°, 0.1).
- [ ] Parent and child both selected: the child is written through its parent
      only (moves once); unselected children follow their parent.
- [ ] One drag produces exactly one undo entry; no entry for a no-op or cancel.
- [ ] A result that cannot be stored as TRS (shear under a non-uniformly scaled
      parent) is rejected without partial writes, and the UI shows the reason.
- [ ] While the gizmo is hovered or dragged, camera and selection input are
      blocked; hiding the UI, focus loss or a world/document change cancels the drag.
- [ ] ImGuizmo types stay private to `src/app/Sandbox`; runtime owns preview,
      multi-selection math and the undo transaction; layering stays strict.
- [ ] CPU contract tests cover pivot math, hierarchy rule, rejection and undo;
      the Sandbox integration suite covers UI activation and drag; a Vulkan
      acceptance smoke shows a visible group move and its undo.
- [ ] READMEs, ADR 0006 and affected architecture docs describe the new frontend.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(GizmoInteraction|GizmoInteractionEngineWiring|SceneInteractionModule|EditorCommandHistory|EditorUiHost|EditorUiModule|ImGuiAdapterEngineWiring|RuntimeEngineLayering|RuntimeEnginePrivateGlue|SelectionSnapshotExtraction|SandboxEditorPresentation|SandboxEditorGizmo|RenderWorldContract)\.'
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure --timeout 120 --no-tests=error -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.(ImGuizmo.*|InspectorTransformEditShiftsReferenceTrianglePixels)$'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/generate_session_brief.py --check
```

## Context
Codex plan (2026-10-06, read-only on `3a47bde17`), condensed:

- **Split.** ImGuizmo replaces the current hit-test, mouse-drag and draw
  frontend. `Runtime.GizmoInteraction` stays the owner of preview, group math
  and the undo transaction (reuse `DragCommit`/`DragCancel` and
  `ExecuteUndoableEntityMutation(...TargetAlreadyApplied)`). The ray/pick API,
  `m_AxisLock` and `TransformGizmoRenderPacketBuilder` go only where ImGuizmo
  replaces them; the general graphics packet contracts stay.
- **Existing defects to fix on the way.** `ComputePivot()` averages local
  positions although it documents world positions; rotation/scale do not move
  entity origins around the group pivot.
- **Math.** Freeze selection, pivot, mode and basis at drag start. With start
  gizmo matrix G0, current Gt and each start world matrix Wi0:
  D = Gt·G0⁻¹, Wi' = D·Wi0; convert back with the existing TRS helper
  (`ECS.Component.Transform.Local.cpp`), which silently drops shear — validate
  and reject instead. Use current authoring world matrices, not a stale cache.
- **Wiring.** `vcpkg.json`/`cmake/Dependencies.cmake`: move `imguizmo` out of
  the windowing feature, link `imguizmo::imguizmo imgui_core_lib`; link
  `imguizmo_lib` PRIVATE to `ExtrinsicSandboxEditor`. `SceneInteractionModule`
  exposes a small runtime-typed model (copied matrices, pivot, availability,
  session identity) plus begin/preview/commit/cancel; remove the automatic
  platform mouse drag. `EditorUiHost` gets a per-frame viewport input request
  merged after `CaptureSnapshot()` in `EditorUiModule` so existing camera/pick
  gates also block during gizmo use. The shell calls ImGuizmo inside the scene
  rectangle with a copied, unjittered view/projection.
- **Keep.** Module names, ECS TRS storage, scene format, inspector transform
  commands, generic `EditorCommandHistory`, camera controllers, picking,
  layering allowlist.
- **Snap/orientation.** Snap defaults stay 0.25, 15°, 0.1 but become UI
  settings (Codex: configurable snap needs its own config path with
  preview/validate/apply). Groups get local orientation too. The gizmo is
  never activated by keyboard.
- **Tests to change/add.** `Test.GizmoInteraction.cpp` (matrix drag cases),
  `Test.GizmoInteractionEngineWiring.cpp`, `Test.SceneInteractionModule.cpp`,
  `Test.EditorUiHost.cpp`, `Test.ImGuiAdapterEngineWiring.cpp`,
  `Test.RuntimeEngineLayering.cpp`, `Test.RuntimeEnginePrivateGlue.cpp`, new
  `SandboxEditorGizmo` suite in `Test.SandboxEditorPresentation.cpp`, and a
  new `ImGuizmo*` case in `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp`.
- **Overlaps (do not absorb).** UI-037 (transform helper), RUNTIME-284/UI-064
  (history), UI-047/UI-048 (shell), GEOM-115 (module names), RUNTIME-305 and
  GRAPHICS-153 (other transform-preview users), RUNTIME-282, UI-076,
  REVIEW-004. REVIEW-007 R16 (unused gizmo accessors) is affected.

## Decisions (operator, 2026-10-06)
1. Pivot: a toggle as in Blender/Unity. The default is the mean of selected world
   origins (cheap, predictable, works for lights/cameras/empty groups). The
   alternative is the mean of world-bounds centers, with origin fallback for
   entities without bounds. Pivot cost was asked about: both are one O(n) pass
   at drag start over already computed world data.
2. Non-TRS results (shear) are rejected with a reason.
3. Scope includes W/E/R shortcuts, configurable snap and local/global for
   groups.
4. Implement after REVIEW-007 is complete.

5. Group local basis (2026-10-06): the averaged rotation of the selected
   entities' world rotations, `Geometry::Rotation::ChordalMean` (Markley
   quaternion-moment mean; closed form, deterministic, sign-invariant), frozen
   at drag start. A single entity uses its own rotation. If the mean is not
   available (`DegenerateInput`, non-finite or failed status), the gizmo falls
   back to the world basis and shows the reason; it never guesses. Runtime may
   import `Geometry.RotationAveraging` (runtime → geometry is allowed). This is
   the first production consumer of GE06; RUNTIME-322 owns the separate
   "align to average" operation.

## Slice plan
Each slice: Codex plan (read-only) → implementation → focused build/tests →
Codex review of the fixed commit → fixes → re-verification.
1. **Runtime transform core (CPU):** freeze selection, pivot (origin mean |
   bounds-center mean with origin fallback), mode and basis (world | single
   local | group ChordalMean) at drag start; group delta `D = Gt·G0⁻¹`,
   `Wi' = D·Wi0`; write only selected entities without a selected ancestor;
   reject non-TRS (shear) results without partial writes; one undo entry per
   changed drag. Fixes the two existing defects (local-position pivot; group
   rotate/scale not moving positions). CPU contract tests.
2. **Viewport input ownership:** per-frame viewport input request in
   `EditorUiHost`, merged after `CaptureSnapshot()` in `EditorUiModule`, so
   camera and pick gates block while the gizmo is hovered or dragged; cancel on
   focus loss, UI hide, world/document change.
3. **ImGuizmo frontend in the Sandbox shell:** link `imguizmo_lib` privately,
   UI-only enable, translate/rotate/scale, W/E/R while active (not in text
   fields), Escape cancels, pivot toggle, local/global, configurable snap
   (config path with preview/validate/apply); remove the old ray/drag frontend
   and `TransformGizmoRenderPacketBuilder` only where ImGuizmo replaces them.
   ImGui integration tests (`SandboxEditorGizmo`).
4. **Docs and Operational evidence:** READMEs, ADR 0006, architecture docs;
   Vulkan acceptance smoke with a real ImGuizmo drag of a group and its undo.

## Progress
- **Slice 1 — runtime transform core (CPU), 2026-10-06: done in `5ba6f9b97`
  after four Codex review rounds (final verdict: approve).** `GizmoInteraction`
  owns one matrix session (`Begin`/`Preview`/`DragCommit`/`DragCancel`): frozen
  selection, origin or bounds-center pivot, world/local/ChordalMean basis with
  explicit fallback, `D = Gt·G0⁻¹` from the start state, writes only for
  selected entities without a selected ancestor, atomic non-TRS rejection
  (shear, perspective row, singular parent), one undo batch. Preview, commit
  and cancel share one write-free session check (registry, target TRS and
  parent world, parent chains and non-target TRS of the whole frozen
  selection, affine `Gt`); a failed commit records nothing and rolls back
  owned previews; no-ops compare the local 3x3 (mirrors) and translation
  within a few ulps. The ray adapter and group packet builder use the same frame. ADR
  0006 amendment, runtime README and catalog contract
  `runtime.gizmo-transform-session` updated.
- **CPU evidence:** `GizmoInteraction.*` in
  `tests/contract/runtime/Test.GizmoInteraction.cpp` (pivot/stale cache,
  bounds pivot, group rotate/scale, descendants, permutations, local/degenerate
  basis, shear rejection, no-op/move-and-return incl. mirrored scale, 100 ticks
  → one undo, cancel, frozen/stale session, invalid selection, ray adapter;
  review regressions `FailedCommitRollsBackOwnedPreviewsWithoutHistory`,
  `CommitAndCancelOnForeignRegistryWriteNothingAndKeepSession`,
  `ReparentingBeforeCommitIsRejectedWithoutHistory`,
  `SelectedDescendantDeletedOrReparentedDuringDragIsConflict`,
  `PerspectiveGizmoMatrixIsRejectedWithoutWrites`,
  `LargeTranslationDoesNotHideShear`,
  `NearlySingularParentIsRejectedWithReasonAndNoWrites`,
  `RealMoveAtLargePositionIsCommittedAndUndoable`,
  `ForeignChangeToSelectedDescendantTransformIsConflict`), plus
  `GizmoInteractionEngineWiring.*`, `SceneInteractionModule.*`,
  `EditorCommandHistory.*`, `RuntimeEngineLayering.*`,
  `RuntimeEnginePrivateGlue.*`, `GeometryRotationAveraging.*`.
- **Slice 2 — viewport input ownership, 2026-10-07: implemented after two
  Codex review rounds (final verdict: approve).** Operator decisions: native window focus only (no
  scene-panel focus); move only `G` to `UiBegin`; no ray hover pass; the
  claim covers mouse and keyboard; cancel also while minimized.
  `EditorUiHost::RequestViewportInput` ORs contribution claims, reset by the
  owner at `UiBegin`, dropped on hide/`SetOperational(false)`, ignored while
  hidden/non-operational; `EditorUiModule` merges it as mouse+keyboard capture
  after `Adapter->CaptureSnapshot()` and publishes the merged
  `CapturesViewportInput`. `SceneInteractionModule` skips the ray driver for a
  claimed frame (capture is not cancel) and cancels a running session via
  `DragCancel` on the visible→hidden transition (`UiBegin`, `Idle`, before the
  viewport driver) and on `Platform::WindowFocusEvent{false}` (new platform
  event: GLFW focus callback, Null `QueueEvent`; Engine republishes it on the
  kernel bus, `ImGuiAdapter` forwards `io.AddFocusEvent`); its `Idle` hook keeps
  minimized frames pumping. `G` now toggles at `UiBegin` independent of ImGui
  keyboard capture (the input action and the `RuntimeInputActionRegistry`
  requirement are gone), so every cancel precedes the single pre-render flush.
  World/document change keeps the existing `ClearWorldBoundState` cancel.
  ADR 0006 amendment, catalog contract, `runtime.md`, runtime/platform READMEs.
- **Slice 2 CPU evidence:** `EditorUiHost.ViewportInputRequests…`,
  `EditorUiModule.ViewportInputRequestMergesAfterAdapterCaptureAndResetsEachFrame`,
  `ImGuiAdapterEngineWiring.{ViewportClaimBlocksCameraAndPickWithoutEndingItsSession,
  HostHideEndsDragBeforeTheTransformFlush, ShortcutHideEndsDragBeforeTheTransformFlush,
  FocusLossWhileMinimizedCancelsDragWithoutHistory}`,
  `SceneInteractionModule.{FrontendViewportClaimOwnsTheSessionAndUiHideCancelsIt,
  WindowFocusLossCancelsPreviewWithoutHistory,
  WorldAndDocumentChangesCancelMatrixPreviewWithoutHistory}`,
  `ImGuiAdapter.PumpedFocusEventsReachImGuiIoInOrder`,
  `NullPlatform.DeliversWindowFocusEventsInOrder`, order checks in
  `RuntimeEnginePrivateGlue.*`; the `G` tests in `EditorUiModule` and
  `SandboxEditorPresentation` now drive the key through the platform queue;
  the claim test also proves a claimed `F` press does not run (mutation-checked).
  No native GLFW focus run yet.
- **Remaining:** slice 3 (ImGuizmo frontend, UI toggles, W/E/R, Escape, snap
  config, retire the ray frontend where replaced, `SandboxEditorGizmo` tests),
  slice 4 (docs and the Vulkan acceptance smoke). All acceptance boxes stay
  open until then.

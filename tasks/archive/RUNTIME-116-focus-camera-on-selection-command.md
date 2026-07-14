---
id: RUNTIME-116
theme: F
depends_on: []
maturity_target: CPUContracted
---
# RUNTIME-116 — Focus-camera-on-selection command (F key)

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Claude on `claude/wizardly-archimedes-f611ix`.
- Branch/PR: `claude/wizardly-archimedes-f611ix`, PR #983, merge commit
  `eab66e0d`.
- Summary: runtime now owns `Extrinsic.Runtime.CameraFocusCommand`, which
  computes deterministic camera focus targets from world bounding spheres or ECS
  entity bounds, applies them through `CameraControllerRegistry`, and exposes
  `FocusCameraOnSelection` for the active selection. `Engine::RunFrame` binds the
  command to `F` after `FlushPreRenderTransformState`, so focus reads refreshed
  `World::Bounds` and rebuilds the render camera on success in the same frame.
- Evidence: verification on 2026-06-18 built `IntrinsicTests`, passed the new
  13-case `Test.RuntimeCameraFocusCommand.cpp`, passed the default CPU gate
  (`2894/2894` tests), and passed layering, test-layout, task-policy,
  doc-links, and docs-sync structural checks.

## Goal
- Add a reusable runtime command that repositions the active camera so a chosen
  set of objects is centered in view and completely visible, and bind it to the
  `F` key ("focus") so it applies to the current entity selection. When several
  entities are selected, the command frames their center of mass and the largest
  enclosing extent so all selected entities fit in view.

## Non-goals
- No new camera framing math inside `ICameraController::Focus` — the existing
  per-controller `Focus(CameraFocusTarget{Center, Radius})` already positions the
  camera to frame a bounding sphere with padding; this task only computes a good
  `CameraFocusTarget` and routes it.
- Do not change the existing import-time auto-focus path
  (`FocusMainCameraOnImportedGeometry` / `ToCameraFocusTarget`) in
  `Runtime.Engine.cpp`; that scoped behavior stays as-is.
- No editor UI button/panel for focus in this task (the reusable function is
  callable from UI later; UI surfacing is owned by the UI theme).
- No rebindable-keymap system; `F` is wired directly like the existing
  WASD/Q/E/Space camera keys.
- No GPU/Vulkan operational proof required.

## Context
- Owning subsystem/layer: `runtime`. The camera controllers
  (`Extrinsic.Runtime.CameraControllers`), the `SelectionController`
  (`Extrinsic.Runtime.SelectionController`), and the per-frame input/camera
  composition (`Engine::RunFrame`) are all runtime-owned, so a command that
  reads the ECS selection + world bounds and drives the camera belongs in
  `runtime`. Runtime may import all lower layers.
- Reused building blocks (already shipped):
  - `ICameraController::Focus(CameraFocusTarget)` implemented by Orbit/Fly/
    FreeLook/TopDown controllers; `CameraControllerRegistry` (slots, with
    `ResolveOrNull` + `MarkCameraTransition`) owned by `Engine`.
  - `SelectionController::SelectedStableIds()` +
    `SelectionController::ToEntityHandle(stableId)`.
  - ECS `Components::Culling::World::Bounds { Geometry::OBB WorldBoundingOBB;
    Geometry::Sphere WorldBoundingSphere; }`, kept current by the
    `BoundsPropagation` system and set on import by `AttachGeometryBounds`.
- Frame ordering: `World::Bounds` is only guaranteed current for the frame after
  `Engine::RunFrame` runs `FlushPreRenderTransformState(*m_Scene)` (BUG-024:
  TransformHierarchy → BoundsPropagation → RenderSync), which applies transform
  edits made during `OnVariableTick` / the ImGui editor hook / the gizmo drag.
  The `F`-key gather must therefore run *after* that flush, otherwise pressing
  `F` right after moving a selected entity frames its stale position/extent.
  - `Platform::Input::Context::IsKeyJustPressed(Key::F)` (F = 70; no existing
    binding — verified no conflict).
- Aggregation semantics (multi-select): treat each selected entity's world
  bounding sphere `(Cᵢ, Rᵢ)` as a unit. Center of mass `C = mean(Cᵢ)`
  (equal weight per entity — deterministic and dependency-free; a volume- or
  mass-weighted variant is a possible future refinement, not needed for framing).
  Largest enclosing extent `R = max_i(|C − Cᵢ| + Rᵢ)`, which guarantees every
  selected entity's bounding sphere is enclosed by `(C, R)`, so the existing
  `Focus` distance formula frames all of them. For a single entity this reduces
  to that entity's own center and radius.
- No ADR is warranted: this introduces no hard-to-reverse layering/format
  decision; it composes already-decided runtime seams. Camera/selection runtime
  handoff rationale already lives in `docs/adr/0006` / `docs/adr/0007`.

## Required changes
- [x] Add `src/runtime/Cameras/Runtime.CameraFocusCommand.cppm` exporting, in
      `namespace Extrinsic::Runtime`:
  - [x] `std::optional<CameraFocusTarget> ComputeFocusTargetForBoundingSpheres(std::span<const Geometry::Sphere>)` — pure center-of-mass + enclosing-radius aggregation; `nullopt` when empty / no finite sphere.
  - [x] `std::optional<CameraFocusTarget> ComputeFocusTargetForEntities(const ECS::Scene::Registry&, std::span<const ECS::EntityHandle>)` — gather valid world bounding spheres and aggregate.
  - [x] `bool ApplyCameraFocus(CameraControllerRegistry&, CameraControllerSlot, const CameraFocusTarget&)` — `Focus` + `MarkCameraTransition`; `false` when the slot has no controller.
  - [x] `bool FocusCameraOnEntities(CameraControllerRegistry&, const ECS::Scene::Registry&, std::span<const ECS::EntityHandle>, CameraControllerSlot = CameraControllerSlot::Main)` — reusable "focus on any objects" command.
  - [x] `bool FocusCameraOnSelection(CameraControllerRegistry&, const SelectionController&, const ECS::Scene::Registry&, CameraControllerSlot = CameraControllerSlot::Main)` — selection-driven wrapper (the `F`-key command).
- [x] Add `src/runtime/Cameras/Runtime.CameraFocusCommand.cpp` with the
      implementations (radius clamped to a small minimum, non-finite guarded).
- [x] Register both files in `src/runtime/CMakeLists.txt` (CXX_MODULES + PRIVATE).
- [x] Wire the `F` key in `src/runtime/Runtime.Engine.cpp` `RunFrame` Phase 4,
      **after** `FlushPreRenderTransformState(*m_Scene)` so the gather reads
      refreshed `World::Bounds` (BUG-024 ordering), on
      `m_Config.Camera.Enabled && !imguiCapturesKeyboard && IsKeyJustPressed(Key::F)`:
      call `FocusCameraOnSelection(m_CameraControllers, m_SelectionController, *m_Scene)`
      and, on success, rebuild `renderInput.Camera` from the Main controller so the
      reframed camera reaches the gizmo packets and render extraction the same frame.
- [x] Import the new module in `Runtime.Engine.cpp`.

## Tests
- [x] Add `tests/contract/runtime/Test.RuntimeCameraFocusCommand.cpp`:
  - [x] `ComputeFocusTargetForBoundingSpheres`: empty → `nullopt`; single sphere → its center/radius; two separated unit spheres → midpoint center and radius enclosing both; assert the enclosing invariant `|C − Cᵢ| + Rᵢ ≤ R + eps` for every input; degenerate/non-finite inputs are guarded.
  - [x] `ComputeFocusTargetForEntities`: a real `ECS::Scene::Registry` with two entities carrying `Culling::World::Bounds`; entities without bounds are skipped; empty/none → `nullopt`.
  - [x] `FocusCameraOnEntities` / `FocusCameraOnSelection`: against a `CameraControllerRegistry` holding a recording `ICameraController`, assert `Focus` is called with the aggregated target and a camera transition is marked; no controller / empty selection → `false` and no call.
- [x] Register the test in `tests/CMakeLists.txt` `_runtime_contract_test_files`
      (inherits `contract runtime` labels).
- [x] Run the default CPU gate; confirm the new test runs and passes.

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md` (new module surface).
- [x] Regenerate `tasks/SESSION-BRIEF.md` (task opened/retired).
- [x] Add a brief note of the reusable focus command + `F` binding to
      `docs/architecture/runtime.md` where camera/input composition is described.
- [x] On completion: add a `## Completion` block, move this file to
      `tasks/done/`, and append a narrative to `tasks/done/RETIREMENT-LOG.md`.

## Acceptance criteria
- [x] `FocusCameraOnSelection` repositions the `Main` camera so all selected
      entities' world bounds are enclosed by the computed focus sphere (center of
      mass + enclosing extent) and marks a camera transition.
- [x] Single-entity selection focuses that one object; a selection with no
      bounded entities (or empty) is a no-op returning `false`.
- [x] The `F` key triggers focus-on-selection once per key-down edge and is
      suppressed while Dear ImGui owns the keyboard.
- [x] New contract test passes under the default CPU gate and is labeled
      `contract runtime`.
- [x] `python3 tools/repo/check_layering.py --root src --strict` passes (no new
      illegal dependency edges); module inventory + session brief regenerated.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -R 'RuntimeCameraFocusCommand|RuntimeCameraControllers'
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing the `ICameraController::Focus` distance formula or the import-time
  auto-focus path behavior.
- Adding `ecs`/`graphics` imports to the `Extrinsic.Runtime.CameraControllers`
  module (keep the aggregation in the new `CameraFocusCommand` module instead).
- Adding new third-party dependencies or a GPU/Vulkan requirement.

## Maturity
- Target: `CPUContracted`. The reusable aggregation (`Compute*`), the apply step
  (`ApplyCameraFocus`), and the command wrappers (`FocusCameraOnEntities` /
  `FocusCameraOnSelection`) are fully covered by the new CPU contract test.
- The `F`-key→`Engine::RunFrame` binding is thin glue over already-operational
  paths (`IsKeyJustPressed` edge detection and `ICameraController::Focus`, the
  latter already exercised by `Test.RuntimeCameraControllers.cpp`), exercised
  manually / in display-providing lanes exactly like the existing WASD/Q/E/Space
  camera keys. No separate `Operational` follow-up is owed.

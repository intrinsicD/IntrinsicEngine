---
id: UI-080
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
contract_schema: 1
contracts: []
contract_review: >-
  App-layer Inspector widget over the existing ApplyEditorTransformEdit command,
  which already supports SetRotation. It does not drive TRS through
  GizmoInteraction (runtime.gizmo-transform-session) and touches no geometry
  element-domain, property, parameterization, or method-integration surface.
---
# UI-080 — Editable local rotation in the Sandbox Inspector

## Goal
- The Inspector edits local rotation the same way it already edits local
  position and scale, so rotating an entity does not require the viewport gizmo.

## Context
- `Sandbox.EditorShell.cpp` (`scene.inspector` window) edits position and scale
  with `DragFloat3` through `ApplyEditorTransformEdit`, but shows rotation only
  as read-only quaternion text (`DrawQuat("Local rotation (wxyz)", ...)`).
- `EditorTransformEditCommand` already has `SetRotation`/`Rotation`; no runtime
  change is needed.
- Pitfall: re-deriving Euler angles from the quaternion every frame makes the
  widget jump or flip near ±90° pitch. Keep the edited Euler value in UI state
  while the widget is active and derive it from the quaternion only when idle.
- Out of scope: undo-history granularity during drags (position and scale share
  the current behavior) and View-menu grouping (`UI-048`).

## Acceptance criteria
- [ ] Inspector shows local rotation as editable XYZ Euler degrees; an edit
      submits `ApplyEditorTransformEdit` with `SetRotation` and goes through the
      same history/undo path as position and scale.
- [ ] Dragging pitch through ±90° does not jump or flip the displayed values.
- [ ] Without an editor context the Inspector keeps a read-only rotation display.
- [ ] A Sandbox editor test changes rotation through the Inspector path, asserts
      the entity's `Transform` rotation, and asserts undo restores it; the test
      fails with the widget's edit call removed.

## Verification
```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci -R 'SandboxEditorPresentation' --output-on-failure
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

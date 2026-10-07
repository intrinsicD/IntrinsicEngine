---
id: BUG-236
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up of an accepted UI-078 limitation; evidence is the diff, tests, and CI
contract_schema: 1
contracts: [runtime.gizmo-transform-session]
contract_review: The fix changes how the Sandbox ImGuizmo frontend hit-tests scale handles, which the gizmo-transform-session contract covers; the runtime matrix session is unaffected.
---
# BUG-236 — Top-down orthographic X/Z scale handles cannot be picked

## Goal
- In an exactly top-down orthographic view, the Sandbox gizmo's X and Z scale
  handles can be grabbed like in any other view.

## Context
- Found in UI-078 slice 3c (`b513f2787`), accepted as a known limitation when
  UI-078 retired (2026-10-07); documented in ADR 0006 §3 and the Sandbox
  README.
- Repro: Sandbox, top-down camera (orthographic), select an entity,
  Gizmo → Enabled, R (scale). Hovering or pressing the X or Z axis handle does
  nothing; the center (uniform) handle, translate and rotate work.
- Cause: ImGuizmo's `GetScaleType` (`ImGuizmo.cpp`) intersects the pick ray
  with the plane through the gizmo origin whose normal is the axis. Orthographic
  rays along −Y are parallel to the X and Z planes, so there is no
  intersection. Any orthographic view looking along an axis perpendicular to
  the scale axis has the same gap; perspective views only where the plane
  contains the eye (`95d093bb1` moved a `SandboxEditorGizmo` case off such a
  pose).
- Workaround: tilt the view slightly, scale uniformly with the center handle,
  or edit scale in the Inspector.
- Options: (a) patch ImGuizmo through a vcpkg overlay port to fall back to a
  screen-space distance test against the projected handle segment when the
  ray is parallel to the plane, and offer it upstream; (b) a frontend-side
  handle hit test before `Manipulate`. Not: a tilted pick camera, which would
  break the camera match the Vulkan smokes prove.

## Acceptance criteria
- [ ] In the top-down orthographic view the X and Z scale handles can be
      grabbed and drag the selected group's scale; perspective behavior is
      unchanged.
- [ ] A `SandboxEditorGizmo` case drags the X and Z scale handles in the
      top-down orthographic view and fails without the fix.
- [ ] ADR 0006 §3 and the Sandbox README drop the limitation.

## Verification
```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^SandboxEditorGizmo\.'
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

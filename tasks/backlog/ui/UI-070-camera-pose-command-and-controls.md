---
id: UI-070
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI/runtime slice; evidence is the diff, contract and ImGui tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality]
---
# UI-070 — Camera pose command, view presets and Focus selection

## Goal
Users and agents can set the viewport camera to an explicit pose, a view preset
or a fit-to-entity focus through one editor command.

## Context
- Gap found in RUNTIME-312 slice 7F (2026-10-02). No editor command sets a
  camera pose, preset or focus:
  - The viewport camera is driven only by input.
  - `ApplyEditorCameraControllerCommand` switches only the controller kind.
  - View presets (`front`, `top`, `isometric`, ...) exist only inside
    `ViewCaptureModule`, which restores the view after a screenshot.
  - `FocusCameraOnEntities` needs a `CameraControllerRegistry&` that the
    editor command surface does not expose.
- UI parity rule (ADR 0029): the UI action comes first, then the agent's
  `set_camera` gains `pose`/`preset`/`focus` over the same command.
- Reuse the ViewCapture preset framing math instead of duplicating it.

## Decisions (2026-10-02)
- The registry premise was stale: `EditorSceneEditingContext::CameraControllers` already carries it.
- Preset axes and the seed recipe moved into `Runtime.CameraControllers` (not `Runtime.CameraFocusCommand`): `Runtime.SceneEditingOperations` must stay scene-free, which the `EditorCompilationLocality.SceneRegistryBorrows` guard enforces; view capture calls them.
- Camera pose, preset and focus changes are view state and do not enter the undo history, like `ApplyEditorCameraControllerCommand`.
- Orbit parameters (yaw/pitch/radius) are descoped as a separate input: `Position = Target - direction * radius` with `Target`, `Up` expresses every orbit pose, and the yaw/pitch convention is controller-private. `Pose` mode keeps `Target` as the orbit pivot.
- Per controller kind: orbit exact (radius clamped and reported), free-look exact (roll from `Up`), fly applies position and direction with `Up` as a hint, top-down only looks along -Y. Unreachable directions return `UnsupportedCameraPose` without touching the camera.

## Acceptance criteria
- [ ] A runtime editor command in `Runtime.SceneEditingOperations` applies a pose (position/target/up or orbit parameters), a named preset, or a fit to one or more stable entity ids. It shares preset/framing code with `ViewCaptureModule`.
- [ ] The Sandbox UI exposes the presets and "Focus selection" (View menu or Camera panel) through that command.
- [ ] The agent `set_camera` accepts `pose`, `preset` and `focus` over the same command, and the lane doc Naming/Limitations are updated.
- [ ] Tests: command contract (pose round-trip, preset framing equals the ViewCapture framing, focus on off-origin bounds); ImGui action test; agent test.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Camera|ViewCapture|SandboxAgentServer|AgentOperations|SandboxEditorSession' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

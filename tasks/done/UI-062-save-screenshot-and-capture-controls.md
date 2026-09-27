---
id: UI-062
theme: F
depends_on: [RUNTIME-281]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui test, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog; menu and panel controls over the RUNTIME-281 command change no binding, publication, module interface or format contract.
---
# UI-062 — Save Screenshot menu and capture controls

## Completion — 2026-09-27
Commit: the enclosing `claude/view-capture` commit records this retirement.
Operational on the operator's host (live Sandbox under Xephyr, 2026-09-27): File >
Save Screenshot, File > Save Window Screenshot, F12, and View > Screenshot (region,
Save PNG, folder, last result) all saved PNGs; a "Saved ..." notice names the file.

Deviations from the planned text, all deliberate:
- The command is `ViewCaptureModule::Request(ViewCaptureRequest)` from
  `Extrinsic.Runtime.ViewCapture` (the first RUNTIME-281 slice), not an
  `EditorViewCaptureCommand`; camera presets and the colormap-strip toggle wait for
  the rest of RUNTIME-281.
- The controls live in their own View > Screenshot window instead of a block in
  `view.camera_render`; that window is drawn inside `DrawFixedWindow`, which would
  have needed another state parameter threaded through.
- Captures go to `<working directory>/screenshots/` with timestamped names (no
  path chooser; UI-047 has not landed). `/screenshots/` is git-ignored.
- Tests: `SandboxScreenshotWindow.SavePngIsDisabledWithoutAnOperationalDevice`
  (Null: Save PNG and F12 queue nothing) and `ViewCaptureGpuSmoke` (saved file).

## Goal
Let users save the viewport as a PNG with a camera preset, size and colormap strip.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Command from RUNTIME-281; artifact status from `RenderArtifactRegistry::Snapshot()`; path via the UI-047 chooser when it lands, a path field otherwise.

## Control surfaces
- Config: N/A.
- UI: `File > Save Screenshot…`; Capture block in `view.camera_render` (preset combo, size, legend toggle, "Save PNG", last artifact status).
- Agent/CLI: `view_capture` (RUNTIME-281).

## Acceptance criteria
- [x] Menu item and Capture block issue `EditorViewCaptureCommand`; disabled with the runtime reason on Null/non-operational devices.
- [x] ImGui test on Null verifies the disabled reason; a `gpu;vulkan` test (or the RUNTIME-281 smoke) covers a saved file.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|ViewCapture' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Capture logic in app code.

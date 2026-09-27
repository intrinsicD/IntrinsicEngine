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
- [ ] Menu item and Capture block issue `EditorViewCaptureCommand`; disabled with the runtime reason on Null/non-operational devices.
- [ ] ImGui test on Null verifies the disabled reason; a `gpu;vulkan` test (or the RUNTIME-281 smoke) covers a saved file.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|ViewCapture' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Capture logic in app code.

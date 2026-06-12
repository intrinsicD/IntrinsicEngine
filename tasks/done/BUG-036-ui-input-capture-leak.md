---
id: BUG-036
theme: F
depends_on: [RUNTIME-090, UI-001]
maturity_target: CPUContracted
---
# BUG-036 — UI-captured input leaks into engine controls

## Goal
- Stop mouse and keyboard input captured by Dear ImGui UI from also driving runtime camera, gizmo, and viewport-selection systems in the same engine frame.

## Non-goals
- No platform event ownership change; platform windows still publish raw events and maintain their raw input context.
- No broad editor-input binding redesign or GLFW key-code mapping slice.
- No graphics, shader, RHI, or Vulkan behavior changes.
- No change to application-level `IApplication::OnVariableTick` input APIs.

## Context
- Owner/layer: `runtime` owns Dear ImGui composition (`ImGuiAdapter`) and the engine-frame wiring for camera controllers, transform gizmos, and selection requests.
- The adapter already pumps platform events into ImGui and exposes `WantsMouseCapture()`, but `Engine::RunFrame()` only uses that for viewport selection. Camera controllers and the transform gizmo still consume `Platform::Input::Context` directly.
- Keyboard capture is not exported by the adapter, so text fields cannot prevent keyboard-driven camera movement.

## Completion
- Completed: 2026-06-12. Commit/PR: this retirement commit.

## Required changes
- [x] Add a backend-neutral regression proving Dear ImGui capture state is surfaced by the runtime adapter.
- [x] Gate runtime camera updates on ImGui mouse or keyboard capture while preserving the current camera snapshot.
- [x] Gate runtime gizmo hit-test/drag/modifier input on ImGui capture.
- [x] Keep viewport-selection suppression using the same captured mouse state.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Add or update `contract;runtime` and/or `integration;runtime` coverage at the narrowest correct seam.
- [x] Run focused runtime/ImGui regression tests.
- [x] Run the default CPU-supported correctness gate or the strongest feasible subset.

## Docs
- [x] Update runtime docs if the public adapter/runtime behavior changes.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retiring the task.

## Acceptance criteria
- [x] UI-hovered/captured mouse input does not submit viewport picks or drive transform gizmo interaction.
- [x] UI-focused keyboard input does not move runtime camera controllers.
- [x] The adapter exposes both mouse and keyboard capture state without exporting ImGui headers.
- [x] Existing runtime camera/gizmo behavior remains unchanged when ImGui does not capture input.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapter|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

2026-06-12 results:
- Pre-fix red gate: `ImGuiAdapterEngineWiring.UiCaptureSuppressesRuntimeInputConsumers` failed because the camera
  controller saw raw keyboard/mouse input and the transform gizmo saw the Shift modifier while ImGui capture was active.
- Focused regression: `ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapter(\.ExposesMouseAndKeyboardCaptureRequests|EngineWiring\.UiCaptureSuppressesRuntimeInputConsumers)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 2/2 after the fix.
- Runtime subset: `ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapter|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 22/22.
- Default CPU gate: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 2990/2990.
- Structural checks passed: task policy strict, doc links, layering strict, test layout strict, docs sync diff-mode, and
  `git diff --check`.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Moving UI ownership into graphics, platform, or ECS layers.
- Filtering or mutating the platform window's raw input context globally.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed because this is backend-neutral runtime input routing covered by the default CPU gate.

---
id: BUG-037
theme: F
depends_on: [RUNTIME-090, UI-001]
maturity_target: CPUContracted
---
# BUG-037 — Window close can leave runtime running

## Goal
- Make OS/platform window-close requests terminate the runtime loop deterministically, even after camera, triangle-selection, gizmo, or ImGui interactions.

## Non-goals
- No renderer, shader, RHI, Vulkan, or swapchain behavior changes.
- No platform backend replacement or new windowing API.
- No UI panel close semantics change; this task is about the application window close request.
- No camera-controller, transform-gizmo, or selection feature changes except as needed to reproduce the close path.

## Context
- Owner/layer: `runtime` owns the engine loop and composes the `platform` window port; `platform` owns raw close events and the `IWindow::ShouldClose()` flag.
- GLFW and Null windows both set `ShouldClose()` when a `WindowCloseEvent` is applied. `Engine::RunFrame()` handles close before renderer work, but `Engine::Run()` can also exit directly from its loop condition when `m_Window->ShouldClose()` is true.
- Before this fix, the `Run()` loop did not normalize that direct `ShouldClose()` exit into `m_Running == false`, so engine state could report running even though the application window had requested close.

## Completion
Completed: 2026-06-12. Commit/PR: this retirement commit.

## Required changes
- [x] Add a deterministic regression proving `Engine::Run()` leaves `IsRunning()` false when the window becomes closed before entering a frame.
- [x] Add a regression covering a close request after representative camera/UI/selection input has occurred.
- [x] Normalize `Engine::Run()` exit state so any observed window close request calls `RequestExit()`.
- [x] Keep the existing `RunFrame()` early close branch before renderer work.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Add or update `contract;runtime` coverage at the engine/window seam.
- [x] Run the focused runtime close regression.
- [x] Run the relevant runtime/ImGui/selection subset.
- [x] Run the default CPU-supported correctness gate or the strongest feasible subset.

## Docs
- [x] Update runtime docs if the frame-loop state contract changes.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening and retiring the task.

## Acceptance criteria
- [x] `Engine::Run()` reports `IsRunning() == false` after it returns because the platform window requested close.
- [x] Closing before the first frame and closing after interactive input both converge through the same runtime exit state.
- [x] Close handling remains backend-neutral and does not depend on Dear ImGui capture state.
- [x] No renderer work is performed after the close request is observed at the platform-frame boundary.

## Verification
```bash
# Red gate before the fix:
#   ImGuiAdapterEngineWiring.RunNormalizesNativeCloseBeforeFirstFrame failed with engine.IsRunning() == true.
#   ImGuiAdapterEngineWiring.RunNormalizesNativeCloseAfterInteractiveInput failed with engine.IsRunning() == true.

cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapterEngineWiring\.RunNormalizesNativeClose' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2/2 tests.

ctest --test-dir build/ci --output-on-failure -R 'ImGuiAdapterEngineWiring|SandboxEditorUi\\.PlatformCloseEventStopsEngineRunState|RuntimeFrameLoop|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 7/7 tests.

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2992/2992 tests.

python3 tools/agents/check_task_policy.py --root . --strict
# Passed.

python3 tools/docs/check_doc_links.py --root .
# Passed.

python3 tools/repo/check_layering.py --root src --strict
# Passed.

python3 tools/repo/check_test_layout.py --root . --strict
# Passed.

git diff --check
# Passed.

git diff -- . | grep '\\[DBG-'
# Passed: no matches.
```

## Forbidden changes
- Moving close-event ownership into graphics, ECS, or UI.
- Filtering or mutating the platform raw input context globally.
- Using backend-specific GLFW calls in promoted runtime code.
- Treating a UI widget/window close as equivalent to the OS/application window close request.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed because the close-state invariant is backend-neutral and covered through the runtime/window contract seam.

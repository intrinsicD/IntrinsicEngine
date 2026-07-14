# RUNTIME-081A — Camera legacy gap analysis and promoted controller baseline

## Goal
- Compare all camera-related legacy surfaces against promoted runtime/graphics layers, then land a CPU-only promoted baseline for runtime-owned camera controllers.

## Non-goals
- No graphics-side input polling.
- No editor UI camera shortcuts or gizmo hit testing.
- No secondary/preview/top-down multi-camera composition policy in this slice.
- No mechanical file moves.

## Context
- Owner/layer: `runtime` owns camera controller state and input-to-camera updates; `graphics/renderer` owns immutable `CameraViewInput`/`CameraViewSnapshot` validation; `src/legacy` remains the compatibility source for `Graphics::CameraComponent`, `FlyControlComponent`, `OrbitControlComponent`, and camera-space helpers.
- Status: done (2026-05-13).
- Legacy baseline: `src/legacy/Graphics/Graphics.Camera.cppm` exports `CameraComponent`, perspective/orthographic projection state, fly/orbit controller component PODs, camera-space transforms, and ray helpers. `src/legacy/Graphics/Graphics.Camera.cpp` implements matrix updates, resize handling, fly look/movement, orbit rotation/zoom/pan.
- Promoted state before this task: `src/graphics/renderer/Graphics.CameraSnapshots.cppm` validates immutable camera snapshots and pick rays; `src/runtime/Runtime.ReferenceScene.cpp` finalizes a static reference-scene camera seed; `Runtime.Engine` directly substitutes that seed into `RenderFrameInput::Camera`. No promoted runtime camera controller updates user input.
- Follow-up anchor: `tasks/archive/RUNTIME-081-camera-controllers.md` completed the broader controller-family and named-slot work. This task records remaining gaps outside the runtime controller surface.

## Required changes
- [x] Inspect legacy camera component, matrix update, fly/orbit controller, and camera-space helper behavior.
- [x] Inspect promoted camera snapshot, render-frame, reference-scene, and runtime engine camera paths.
- [x] Document gaps between `src/legacy` behavior and promoted runtime/graphics camera ownership.
- [x] Add a promoted runtime `Extrinsic.Runtime.CameraControllers` module with runtime-owned `ICameraController`, orbit/fly/free-look/top-down controllers, and a simple registry.
- [x] Wire `Runtime.Engine` to update the selected camera controller from `Platform.Input::Context` and submit its `CameraViewInput` each frame.
- [x] Preserve the reference-scene camera as the controller seed rather than a direct render-input substitution.

## Tests
- [x] Add CPU-only runtime contract tests for all four controller families, orbit radius/yaw guards, fly delta-time scaling, top-down orthographic zoom clamp, free-look roll, registry replacement seeding, and named multi-camera slots.
- [x] Run focused CPU runtime contract tests.
- [x] Run layering and docs-link checks for touched scope.

## Docs
- [x] Update `src/runtime/README.md` with current camera-controller status and remaining gaps.
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [x] Runtime has no promoted camera input dependency in `graphics/*`; controller input handling is under `runtime`.
- [x] The default runtime camera path produces finite, invertible `CameraViewInput` without relying on direct reference-camera substitution.
- [x] Orbit, fly, free-look, and top-down controllers have deterministic CPU tests.
- [x] Verification commands pass or any environment limitation is recorded.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R IntrinsicRuntimeContractTests -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

Current verification notes:

- `cmake --preset ci` failed because `clang-20` / `clang++-20` are not present on `PATH`.
- `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++` configured with Clang 22.0.0, but `cmake --build --preset ci --target IntrinsicRuntimeContractTests` was blocked during regeneration by pre-existing Draco cache errors (`No SOURCES given to target: draco_*`).
- Supplemental out-of-source Clang 22 debug build passed: `cmake --build /home/alex/Documents/IntrinsicEngine/cmake-build-debug --target IntrinsicRuntimeContractTests -j 4`.
- Supplemental direct GoogleTest run passed after completing all controller families: `/home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/IntrinsicRuntimeContractTests` (`46` tests).
- `ctest` against `cmake-build-debug` was blocked by that tree's GoogleTest discovery metadata referencing an incompatible CLion CMake 4.2 script; the binary was run directly instead.
- Structural checks passed: `python3 tools/repo/check_layering.py --root src --strict`, `python3 tools/docs/check_doc_links.py --root .`, and `python3 tools/agents/check_task_policy.py --root . --strict`.

## Forbidden changes
- Polling input from `src/graphics/*`.
- Adding camera input handling to `src/app` or legacy editor UI.
- Adding gizmo hit-test code.
- Mixing mechanical moves with semantic edits.

## Completion metadata
- Completion date: 2026-05-13.
- Commit reference: pending current workspace/PR.

## Gap analysis
- **Legacy camera component:** `CameraComponent` owns mutable position/orientation/projection matrices and helper methods. Promoted graphics intentionally does not own this mutable state; it consumes immutable `CameraViewInput` and validates `CameraViewSnapshot`.
- **Legacy fly controller:** implemented against `Core.Input::Context` with RMB mouse look, WASD/Space movement, and shift sprint. Promoted runtime had no equivalent; this task adds `FlyCameraController` against `Extrinsic.Platform.Input::Context`.
- **Legacy orbit controller:** implemented against `Core.Input::Context` with RMB orbit, scroll zoom, and WASD target panning. Promoted runtime had no equivalent; this task adds `OrbitCameraController` with distance clamp, yaw wrapping, pitch clamp, scroll zoom, and horizontal panning.
- **Legacy camera systems:** legacy updates camera matrices in-place and extraction copies the component into legacy render packets. Promoted systems use `Runtime.Engine` to build `RenderFrameInput` and `Graphics.Renderer` to derive immutable snapshots. Before this task, engine used direct reference-camera substitution; after this task, the controller owns the camera state and produces the frame camera.
- **Projection coverage:** legacy supports perspective and orthographic projection on `CameraComponent`. The promoted controller surface now covers perspective orbit/fly/free-look and orthographic top-down output.
- **Multi-camera coverage:** legacy/editor scenarios imply active editor camera only. Promoted renderer still consumes one `CameraViewInput` per `RenderFrameInput`; the runtime registry now exposes named camera slots (`Main`, `Preview`, `TopDown`, `EditorSecondary`), but policy for rendering multiple camera outputs in one frame remains future work outside this task.
- **Coordinate helpers:** legacy helper functions (`WorldToScreen`, `RayFromScreen`, etc.) remain tested in `Test_CameraSpaceTransforms.cpp`. Promoted equivalent snapshot/pick-ray behavior is centralized in `BuildCameraViewSnapshot`; no new graphics-side helper dependency is introduced.




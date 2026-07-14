# RUNTIME-081 — `Extrinsic.Runtime.CameraControllers` umbrella

## Goal
- Complete the runtime-side camera controller umbrella declared by `GRAPHICS-017Q`: `Extrinsic.Runtime.CameraControllers` hosts orbit, fly, free-look, and top-down controllers plus named controller slots. Controllers read platform input through the existing `Platform.Input` port, translate it into runtime-owned camera state, and runtime fills `CameraViewInput` for renderer extraction.

## Non-goals
- No graphics-side input polling.
- No editor camera UI surfaces.
- No multi-camera composition policy beyond the GRAPHICS-017Q "one `CameraViewInput` per frame per camera" rule.
- No transform-gizmo hit testing (that is `RUNTIME-084` `GizmoInteraction`).

## Context
- Status: done (2026-05-13); implemented by `tasks/archive/RUNTIME-081A-camera-legacy-gap-analysis.md` and this follow-up completion pass.
- Owner/layer: `runtime`.
- Planning anchors: `tasks/archive/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md`, `tasks/archive/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md`.
- Today: `RenderFrameInput::Camera` is populated by the selected runtime camera controller. Orbit, fly, free-look, and top-down are implemented and can be seeded from the reference scene. The registry exposes `Main`, `Preview`, `TopDown`, and `EditorSecondary` slots; `Engine::RunFrame()` drives `Main`.
- Multiple cameras (preview, top-down, editor secondary view) are runtime-owned slots. Rendering more than one camera output in one frame remains a future renderer/runtime composition-policy task, not part of this controller umbrella.

## Required changes
- [x] Add `src/runtime/Cameras/Runtime.CameraControllers.cppm` exporting `Extrinsic.Runtime.CameraControllers` with:
  - `class ICameraController { virtual void Update(const Platform::Input::Context& input, double dt) = 0; virtual Graphics::CameraViewInput GetView(Platform::Extent2D viewport) const = 0; }`,
  - concrete `OrbitCameraController` (target + radius + yaw/pitch, mouse-drag rotation, scroll zoom),
  - concrete `FlyCameraController` (WASD translation + mouse look, optional sprint modifier),
  - concrete `FreeLookCameraController` (WASD + mouse look + roll),
  - concrete `TopDownCameraController` (orthographic, pan + zoom).
- [x] Add a `CameraControllerRegistry` mirroring `ReferenceSceneRegistry`: register a controller for a slot/role, retrieve, swap.
- [x] Wire `Engine::RunFrame` to call `controller->Update(input, dt)` and substitute `controller->GetView(viewport)` into `RenderFrameInput::Camera`. The reference-scene-provided `CameraViewInput` becomes the seed for the controller's initial state.
- [x] **Retire the `GRAPHICS-029B` direct camera substitution.** The transitional `m_ReferenceCamera → RenderFrameInput::Camera` substitution introduced by `GRAPHICS-029B` has been replaced by the controller-driven path; the reference-scene-provided `CameraViewInput` survives only as the controller's initial seed.
- [x] Configuration: `EngineConfig::Camera { Controller = CameraControllerKind::Orbit (default) }`. `CreateReferenceEngineConfig()` keeps `Orbit`.
- [x] Add concrete `FreeLookCameraController` and `TopDownCameraController` implementations.
- [x] Add named multi-camera registry slots beyond `CameraControllerSlot::Main` (`Preview`, `TopDown`, `EditorSecondary`).

## Tests
- [x] `contract;runtime` test: each concrete controller produces a finite, invertible `CameraViewInput` for a representative input sequence.
- [x] `contract;runtime` test: orbit camera radius clamps to a minimum (no inversion through origin); yaw wraps modulo 2π.
- [x] `contract;runtime` test: fly camera respects deltaTime scaling; per-frame translation is independent of frame rate.
- [x] `contract;runtime` test: switching the registered controller mid-frame preserves the previous controller's terminal `CameraViewInput` as the new seed.
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/runtime/README.md` to flip the planned `CameraControllers` umbrella row to current state.
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [x] At least the Orbit controller is fully wired and selected by `CreateReferenceEngineConfig()`.
- [x] All four controllers compile, register, and pass their respective contract tests.
- [x] No new graphics imports; runtime imports `Extrinsic.Platform.Input` for the input port edge.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

Current verification notes:

- `cmake --preset ci` failed because `clang-20` / `clang++-20` are not present on `PATH`.
- `cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++` configured with Clang 22.0.0, but `cmake --build --preset ci --target IntrinsicRuntimeContractTests` was blocked during regeneration by pre-existing Draco cache errors (`No SOURCES given to target: draco_*`).
- Supplemental out-of-source Clang 22 debug build passed: `cmake --build /home/alex/Documents/IntrinsicEngine/cmake-build-debug --target IntrinsicRuntimeContractTests -j 4`.
- Focused direct GoogleTest run passed: `/home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/IntrinsicRuntimeContractTests --gtest_filter='RuntimeCameraControllers.*'` (`8` tests).
- Supplemental direct runtime contract run passed: `/home/alex/Documents/IntrinsicEngine/cmake-build-debug/bin/IntrinsicRuntimeContractTests` (`46` tests).
- Structural checks passed: `python3 tools/repo/check_layering.py --root src --strict`, `python3 tools/repo/check_test_layout.py --root . --strict`, `python3 tools/agents/check_task_policy.py --root . --strict`, and `python3 tools/docs/check_doc_links.py --root .`.

## Forbidden changes
- Polling input from `src/graphics/*`.
- Adding camera input handling to `Sandbox::App`.
- Adding gizmo hit-test code (reserved for `RUNTIME-084`).
- Mutating `RenderFrameInput::Camera` from graphics.

## Completion metadata
- Completion date: 2026-05-13.
- Commit reference: pending current workspace/PR.

## Next verification step
- Future work should open a separate task for rendering multiple camera outputs in one frame if/when a preview/top-down/editor-secondary viewport composition policy is needed.

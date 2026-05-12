# RUNTIME-081 — `Extrinsic.Runtime.CameraControllers` umbrella

## Goal
- Open the runtime-side camera controller umbrella declared by `GRAPHICS-017Q`: a new module `Extrinsic.Runtime.CameraControllers` (planned home: `src/runtime/Cameras/Runtime.CameraControllers.cppm`) hosting concrete controllers (`OrbitCameraController`, `FlyCameraController`, `FreeLookCameraController`, `TopDownCameraController`). Controllers read platform input deltas through the existing `Platform.Input` port, translate them into runtime-owned camera state, and runtime extraction fills `CameraViewInput` and submits it through `IRenderer::SubmitRuntimeSnapshots()`.

## Non-goals
- No graphics-side input polling.
- No editor camera UI surfaces.
- No multi-camera composition policy beyond the GRAPHICS-017Q "one `CameraViewInput` per frame per camera" rule.
- No transform-gizmo hit testing (that is `RUNTIME-084` `GizmoInteraction`).

## Context
- Status: not started.
- Owner/layer: `runtime`.
- Planning anchors: `tasks/done/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md`, `tasks/done/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md`.
- Today: `RenderFrameInput::Camera` is populated from `GRAPHICS-029B`'s reference scene seed (when that lands) but never updated by user input. Camera state cannot move.
- Multiple cameras (preview, top-down, editor secondary view) are runtime-owned and emit one `CameraViewInput` per frame each.

## Required changes
- [ ] Add `src/runtime/Cameras/Runtime.CameraControllers.cppm` exporting `Extrinsic.Runtime.CameraControllers` with:
  - `class ICameraController { virtual void Update(const Platform::InputFrame& input, double dt) = 0; virtual Graphics::CameraViewInput GetView(const Graphics::Viewport& viewport) const = 0; }`,
  - concrete `OrbitCameraController` (target + radius + yaw/pitch, mouse-drag rotation, scroll zoom),
  - concrete `FlyCameraController` (WASD translation + mouse look, optional sprint modifier),
  - concrete `FreeLookCameraController` (WASD + mouse look + roll),
  - concrete `TopDownCameraController` (orthographic, pan + zoom).
- [ ] Add a `CameraControllerRegistry` mirroring `ReferenceSceneRegistry`: register a controller for a slot/role, retrieve, swap.
- [ ] Wire `Engine::OnVariableTick` (or the `BuildRenderFrameInput` helper introduced by `GRAPHICS-029B`) to call `controller->Update(input, dt)` and substitute `controller->GetView(viewport)` into `RenderFrameInput::Camera`. The reference-scene-provided `CameraViewInput` becomes the seed for the controller's initial state.
- [ ] **Retire the `GRAPHICS-029B` direct camera substitution.** The transitional `m_ReferenceCamera → RenderFrameInput::Camera` substitution introduced by `GRAPHICS-029B` (marked `// TODO(RUNTIME-081): superseded by controller-driven update`) must be deleted in the same commit that wires the controller-driven path. The reference-scene-provided `CameraViewInput` survives only as the controller's initial seed; no other consumer remains.
- [ ] Configuration: `EngineConfig::Camera { ControllerKind = ControllerKind::Orbit (default) }`. `CreateReferenceEngineConfig()` keeps `Orbit`.

## Tests
- [ ] `contract;runtime` test: each concrete controller produces a finite, invertible `CameraViewInput` for a representative input sequence.
- [ ] `contract;runtime` test: orbit camera radius clamps to a minimum (no inversion through origin); yaw wraps modulo 2π.
- [ ] `contract;runtime` test: fly camera respects deltaTime scaling; per-frame translation is independent of frame rate.
- [ ] `contract;runtime` test: switching the registered controller mid-frame preserves the previous controller's terminal `CameraViewInput` as the new seed.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to flip the planned `CameraControllers` umbrella row to current state.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] At least the Orbit controller is fully wired and selected by `CreateReferenceEngineConfig()`.
- [ ] All four controllers compile, register, and pass their respective contract tests.
- [ ] No new graphics imports; runtime imports `Extrinsic.Platform.Input` for the input port edge.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Polling input from `src/graphics/*`.
- Adding camera input handling to `Sandbox::App`.
- Adding gizmo hit-test code (reserved for `RUNTIME-084`).
- Mutating `RenderFrameInput::Camera` from graphics.

## Next verification step
- Land the controller interface + at least Orbit, wire the engine call site, exercise the contract tests above.

---
id: RUNTIME-180
theme: F
depends_on:
  - ARCH-016
maturity_target: Operational
---
# RUNTIME-180 — Extract the camera composition module

## Goal
- Move camera-controller, focus, input, and reference-camera-seed ownership out
  of `Runtime.Engine` into one app-composed `CameraModule`.

## Non-goals
- No second viewport, preview-world policy, or new camera-controller interface.
- No rendering, projection, input-binding, or camera-motion behavior change.
- No runtime ownership of app-specific reference-scene entity creation.
- No dependency on a not-yet-demonstrated `WorldSwitchModule`.

## Context
- Owner/layer: `runtime`; controller and viewport state is global, while every
  focus target and reference seed is qualified by the active `WorldHandle` and
  invalidated or reseeded when that world changes.
- Engine currently imports `Graphics.CameraSnapshots`,
  `Runtime.CameraControllers`, `Runtime.ReferenceScene`, and
  `Runtime.ReferenceSceneControl` and exposes camera/reference facades.
- Camera input/focus has a distinct global viewport lifetime from the
  active-world/document state owned by `SceneEditingModule`.
- Initial reference-scene content is application policy: Sandbox boot creates
  it as initial-world content and supplies only an optional camera seed to this
  module.

## Required changes
- [ ] Add one concrete `CameraModule` owning `CameraControllerRegistry`,
      camera-focus input/command behavior, and optional reference camera seed.
- [ ] Register the existing pre-extraction camera update at the appropriate
      runtime frame hook and resolve only the kernel/input capabilities it
      needs.
- [ ] Make active-world changes clear stale focus targets and world-qualified
      seed state deterministically.
- [ ] Move reference-scene entity/provider installation to Sandbox initial-world
      composition; do not add a replacement reference-scene module.
- [ ] Remove camera/reference state, imports, and
      `GetCameraControllerRegistry`, `GetReferenceSceneRegistry`,
      `IsReferenceSceneInstalled`, and `GetReferenceCameraSeed` from Engine.
- [ ] Migrate asset/editor/test consumers to the module's existing concrete
      camera capability rather than an Engine forwarding facade.

## Tests
- [ ] Preserve camera controller, focus, input, and reference-seed behavior.
- [ ] Add module integration coverage for active-world change invalidation and
      one real `Engine::Run()` camera update.
- [ ] Run focused camera/reference-scene/Sandbox coverage, strict layering, and
      the complete default CPU-supported gate.

## Docs
- [ ] Update runtime and Sandbox architecture docs with the global viewport
      scope and app-owned initial-world bootstrap.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine contains no camera-controller or reference-scene domain state,
      imports, or public facades.
- [ ] Camera state is globally owned but cannot retain a stale world target.
- [ ] Sandbox reference content remains behaviorally equivalent and is visibly
      composed by the app.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'Camera|ReferenceScene|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding a camera service wrapper around `CameraControllerRegistry`.
- Moving app-specific scene content back into Engine.
- Retaining Engine camera/reference compatibility getters.

## Maturity
- Target: `Operational`; the composed module must drive the existing runtime
  camera path during an integration-labeled Engine run.

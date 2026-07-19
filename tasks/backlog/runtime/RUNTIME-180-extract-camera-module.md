---
id: RUNTIME-180
theme: F
depends_on:
  - ARCH-016
  - RUNTIME-181
  - RUNTIME-182
maturity_target: Operational
---
# RUNTIME-180 — Extract the camera composition module

## Goal
- Move camera-controller motion/update plus world-bound pose and
  reference-camera-seed ownership out of `Runtime.Engine` into one
  app-composed `CameraModule`.

## Non-goals
- No second viewport, preview-world policy, or new camera-controller interface.
- No rendering, projection, input-binding, or camera-motion behavior change.
- No runtime ownership of app-specific reference-scene entity creation.
- No dependency on a not-yet-demonstrated `WorldSwitchModule`.
- No ownership of the generic `RuntimeInputActionRegistry`, Sandbox-specific
  `F` binding, import-autofocus policy, or selection state.
- No camera service wrapper, forwarding Engine facade, or persistent camera
  state restored when returning to an old world.

## Context
- Owner/layer: `runtime`; the module object has app-global lifetime, while
  controller slots/poses and the optional reference seed are bound to the
  active `WorldHandle`. `CameraFocusTarget` is a synchronous value; the
  durable stale-state risk is the pose/target retained by each controller.
- Engine currently imports `Graphics.CameraSnapshots`,
  `Runtime.CameraControllers`, `Runtime.ReferenceScene`, and
  `Runtime.ReferenceSceneControl` and exposes camera/reference facades.
- Camera input/focus has a distinct global viewport lifetime from the
  active-world/document state owned by `SceneEditingModule`.
- Initial reference-scene content is application policy: Sandbox boot creates
  it as initial-world content and supplies only an optional camera seed to this
  module.
- The current `FramePhase::BeforeExtraction` hook runs after camera, gizmo,
  input-action, and picking work, so it cannot host the camera update without
  reordering behavior. `RUNTIME-182` first settles the one completed editor
  capture snapshot that camera motion must consume.
- `ReferenceSceneControl` does not retain the world that owns its entity
  handles. Teardown after an active-world switch can therefore target a
  different registry. This extraction must replace that ceremony with
  world-qualified app bootstrap/teardown rather than preserve the defect.

## Required changes
- [ ] Add one concrete `CameraModule` owning `CameraControllerRegistry`,
      its active-world binding, and the optional world-qualified reference
      camera seed. Publish the exact registry; do not add a second service
      type.
- [ ] Add the first earned direct `ViewportInput` frame phase at the current
      `PopulateMainCameraForFrame(...)` call site, after viewport/input and the
      completed editor-capture snapshot exist but before gizmo, picking, and
      generic input-action consumers. Its context borrows active config/world,
      platform input, viewport, capture, mutable `RenderFrameInput`, and the
      needed frame timing only.
- [ ] Register camera update at `ViewportInput`; lazily create the selected
      controller from current config and retained seed so camera hot-enable
      behavior remains unchanged.
- [ ] Subscribe to active-world lifecycle and clear every controller slot,
      pose, pending transition, and seed on a world change or retirement. Also
      compare the bound handle in the frame hook so reset is fail-closed before
      a queued event pump; returning to a prior world must not resurrect state.
- [ ] Remove camera coupling from generic input dispatch:
      `RuntimeInputActionServices` no longer carries
      `CameraControllerRegistry*`; the Sandbox `F` action captures the resolved
      concrete registry.
- [ ] Remove the camera pointer from `AssetImportPipelineDependencies` and
      `RuntimeImportCompletedServices`; Sandbox import-autofocus captures the
      resolved registry while import selection still succeeds when camera is
      omitted.
- [ ] Replace the one-provider `IReferenceSceneProvider` /
      `ReferenceSceneRegistry` / `ReferenceSceneControl` ceremony with plain
      runtime bootstrap/teardown functions returning
      `ReferenceScenePopulation`. Sandbox retains
      `{WorldHandle, population}`, creates it exactly once per app
      initialization, supplies its seed to the camera registry, and tears it
      down only through the original world.
- [ ] Remove camera/reference state, imports, and
      `GetCameraControllerRegistry`, `GetReferenceSceneRegistry`,
      `IsReferenceSceneInstalled`, and `GetReferenceCameraSeed` from Engine.
- [ ] Migrate editor/test consumers to
      `Services().Find<CameraControllerRegistry>()`; omitted editor commands
      are unavailable/null and no caller dereferences a missing service.
- [ ] On shutdown, unsubscribe, withdraw the exact registry, clear all
      bindings/controllers/seed, then destroy state. Reinitialize must
      reprovide, resubscribe, and clear the bound-world token unconditionally
      even when `WorldRegistry::Clear()` recreates the same handle bits.
- [ ] Ratchet the exact observed post-`RUNTIME-182` Engine convergence
      snapshot. This slice removes four plain/domain imports and the three
      counted getters `GetCameraControllerRegistry`,
      `GetReferenceSceneRegistry`, and `GetReferenceCameraSeed`;
      `IsReferenceSceneInstalled` is removed but is not a counted `Get*`.

## Tests
- [ ] Preserve controller/focus math, capture suppression, Sandbox `F`
      behavior, import autofocus/selection, first-frame camera, disabled then
      hot-enabled seed use, and reference-content behavior.
- [ ] Cover exact service publication/withdrawal, duplicate conflict,
      shutdown/reinitialize, finite composed first frame, and omission:
      invalid/default `RenderInput.Camera`, no camera commands, generic
      actions/editor still operational, and import selection without focus.
- [ ] Cover active-world reset before the next viewport hook, away/back
      non-resurrection, wrong-world seed rejection, world-retirement reset,
      and same-handle-bits reinitialize.
- [ ] Cover Sandbox reference bootstrap disabled/enabled, exactly-once
      population per initialization, teardown through the original world,
      retired-original-world no-op, and reference content without a camera
      module.
- [ ] Replace provider/registry lifecycle tests with concrete
      bootstrap/population/teardown coverage and move projection assertions to
      the controller/module tests.
- [ ] Add Engine-layering and gate-routing ratchets, then run focused
      camera/reference/input/ImGui/Sandbox coverage, strict layering, and the
      complete default CPU-supported gate.

## Docs
- [ ] Update ADR-0015 as superseded by ADR-0027/RUNTIME-180 where it rejects
      app-owned bootstrap; update ADR-0027, runtime/graphics/config/kernel
      architecture, runtime/core/Sandbox READMEs, parity documentation, and
      live backlog summaries with global module scope, world-bound camera
      state, app-owned initial-world population, and omission behavior.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine contains no camera-controller or reference-scene domain state,
      imports, or public facades.
- [ ] Camera state has app-global ownership but cannot retain a stale
      world-qualified controller pose or seed.
- [ ] The module is optional: no service/update/fallback camera appears when
      omitted, and non-camera input/editor/import behavior remains available.
- [ ] Generic Engine never interprets `ReferenceSceneConfig`; Sandbox
      reference content remains behaviorally equivalent, visibly app-composed,
      and torn down through its owning world.
- [ ] Camera update retains the existing capture-before-camera-before-gizmo /
      picking / input-action order.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'CameraModule|Camera|ReferenceScene|InputAction|ImGuiAdapter|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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
- Reusing `BeforeExtraction` for camera update or introducing a parallel
  capture/provider/filter path.
- Keeping the provider registry for its single production provider or
  restoring controller state automatically when a world is revisited.

## Maturity
- Target: `Operational`; the composed module must drive the existing runtime
  camera path during an integration-labeled Engine run.

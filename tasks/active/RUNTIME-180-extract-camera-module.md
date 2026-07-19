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

## Status

- In progress as of 2026-07-19; owner: Codex team; implementation branch:
  `codex/runtime-180-camera-module`.
- Next gate: land the typed viewport-input schedule and exact world-bound
  camera registry, then migrate Sandbox/reference-scene callers without an
  Engine compatibility facade.
- 2026-07-19 amendment: rebased the implementation contract on the merged
  `RUNTIME-182` editor-capture seam. The camera insertion point is now a
  dedicated typed viewport-input hook rather than a seventh generic frame
  phase, and the world/reset, omission, import, and reference-scene retirement
  contracts are explicit. All implementation work remains open.

## Goal

- Move camera-controller motion/update plus world-bound pose and
  reference-camera-seed ownership out of `Runtime.Engine` into one
  app-composed `CameraModule`.

## Non-goals

- No seventh `FramePhase` value and no widening of
  `RuntimeFrameHookContext`.
- No second viewport, preview-world policy, or new camera-controller
  interface.
- No rendering, projection, input-binding, or camera-motion behavior change.
- No runtime Engine ownership of app-specific reference-scene bootstrap or
  teardown policy.
- No dependency on a not-yet-demonstrated `WorldSwitchModule`.
- No ownership by `CameraModule` of the generic
  `RuntimeInputActionRegistry`, Sandbox-specific `F` binding,
  import-autofocus policy, or selection state.
- No camera service wrapper, forwarding Engine facade, per-world camera-state
  map, or controller/seed resurrection when returning to an old world.
- No retained provider interface or registry for the single reference-scene
  implementation.

## Context

- Owner/layer: `runtime`; the module object has app-global lifetime, while
  controller slots, poses, pending transitions, and the optional seed are
  bound to exactly one active `WorldHandle`.
- After `RUNTIME-182`, Engine has six generic frame phases and one
  frame-owned `EditorInputCaptureSnapshot`. The current camera call site is
  after `UiEndCapture` and `RenderFrameInput` initialization, and immediately
  before `GizmoFrameService::DriveInputForFrame(...)`.
- The existing generic `RuntimeFrameHookContext` intentionally carries
  module-kernel state rather than viewport/render-input state. Adding camera
  fields to it or inventing a generic `ViewportInput` phase would make all
  generic hooks depend on a one-consumer context.
- A dedicated viewport-input registrar is earned by the runtime-module
  composition seam and the stable frame-loop insertion point. Its one narrow
  context is the test-double surface; a generic phase is reconsidered only
  when a second coherent consumer needs the same ordering and data.
- `CameraControllerRegistry` already is the behavior-bearing camera surface.
  Publishing that exact type avoids a wrapper while giving Sandbox/editor
  callers an optional service lookup.
- `ActiveWorldChanged` and `WorldWillBeDestroyed` are the lifecycle signals.
  The viewport hook must still compare its handle with the registry binding
  so stale camera state is reset before any delayed event pump can expose it.
- Initial reference content is application policy. Sandbox boot creates it in
  the initial world, optionally passes its seed to the camera registry, and
  retains the owning world with the population for teardown.
- `ReferenceSceneControl` currently retains entity handles without their
  owning world. Teardown after a world switch can therefore address the wrong
  registry; this task deletes that control object rather than preserving the
  defect.
- The exact post-`RUNTIME-182` Engine convergence baseline is
  `39` plain imports / `17` domain imports / `2` re-exports / `28` distinct
  public `Get*` names.

## Required changes

- [ ] Add the dedicated exported viewport-input callback surface to
      `Extrinsic.Runtime.Module` without modifying `FramePhase` or
      `RuntimeFrameHookContext`:

      ```cpp
      struct RuntimeViewportInputHookContext
      {
          const Core::Config::EngineConfig& Config;
          WorldHandle ActiveWorldHandle;
          const Platform::Input::Context& Input;
          Core::Extent2D Viewport;
          const EditorInputCaptureSnapshot& EditorCapture;
          Graphics::RenderFrameInput& RenderInput;
          double FrameDeltaSeconds;
      };
      ```

      Define the corresponding `RuntimeViewportInputHook` callback alias only;
      do not add a phase enum, dependency object, interface, or forwarding
      service.
- [ ] Extend `EngineSetup` with a `ViewportInputHookRegistrar`, constructor
      wiring, and `RegisterViewportInputHook(RuntimeViewportInputHook)`.
      Reject an empty callback with the existing explicit `Core::Result`
      pattern and return `InvalidState` when no registrar was supplied.
- [ ] Extend `RuntimeModuleSchedule` with one private viewport-input hook
      record plus register/run paths. Finalization sorts these records
      deterministically by module name and then registration sequence;
      `Clear()` removes them and resets the shared sequence. Wire the registrar
      into both registration- and resolution-phase `EngineSetup` construction.
- [ ] Dispatch the viewport-input schedule at the current
      `PopulateMainCameraForFrame(...)` call site: after `UiEndCapture` has
      completed and `RenderFrameInput` has been initialized, before
      `GizmoFrameService::DriveInputForFrame(...)`. Borrow the active config
      and handle, `IWindow::GetInput()`, framebuffer extent, completed capture
      snapshot, mutable render input, and frame delta directly into the typed
      context. Delete `PopulateMainCameraForFrame(...)`; preserve the existing
      capture-before-camera-before-gizmo/picking/input-action order.
- [ ] Extend the exact published `CameraControllerRegistry` with only these
      world-binding operations:

      ```cpp
      void ResetForWorld(WorldHandle world) noexcept;
      Core::Result SetWorldSeed(
          WorldHandle world,
          std::optional<Graphics::CameraViewInput> seed) noexcept;
      WorldHandle BoundWorld() const noexcept;
      std::optional<Graphics::CameraViewInput>
          WorldSeedFor(WorldHandle world) const noexcept;
      ```

      `ResetForWorld` clears every controller slot, pose, pending transition,
      and seed before binding `world`, even when the new handle bits equal the
      old bits. `SetWorldSeed` rejects an invalid/unbound or non-matching world
      without mutation. `WorldSeedFor` returns a seed only for the current
      valid binding. An invalid reset is the shutdown state. Do not retain a
      per-world map or resurrect state.
- [ ] Add concrete
      `src/runtime/Cameras/Runtime.CameraModule.cppm` and matching `.cpp`
      implementation units. In `OnRegister`, bind
      `setup.Worlds().ActiveWorld()`, publish the exact
      `CameraControllerRegistry`, subscribe to `ActiveWorldChanged` and
      `WorldWillBeDestroyed`, and register the dedicated viewport-input hook.
      Partial registration must fail closed and undo subscriptions,
      publication, and binding.
- [ ] In the camera hook, compare
      `context.ActiveWorldHandle` with `BoundWorld()` and call
      `ResetForWorld(context.ActiveWorldHandle)` on any mismatch before
      reading config/seed/controller state. Preserve lazy construction of the
      configured main controller, disabled-then-hot-enabled seed use, capture
      suppression, view construction, and one-shot transition consumption.
      A missing/invalid active handle produces no camera.
- [ ] On active-world change, reset and bind the new current handle. On
      destruction of the bound world, reset to an invalid handle. On shutdown,
      unsubscribe both lifecycle tokens, withdraw the exact registry, reset to
      invalid, and destroy module state. Reinitialize must start empty even
      when `WorldRegistry::Clear()` later recreates identical handle bits.
- [ ] Compose `CameraModule` in the default Sandbox module list. During
      Sandbox initialization, resolve `CameraControllerRegistry` once and keep
      the result optional for Sandbox policies/reference seeding. Register the
      Sandbox `F` input action only when that lookup succeeds; its callback
      captures the registry rather than receiving it through generic action
      services.
- [ ] Remove `CameraControllerRegistry*` from
      `RuntimeInputActionServices` and remove the camera parameter/field from
      generic input-action dispatch and its Engine call site. Other input
      actions, config, selection, render-input mutation, and capture filtering
      remain operational when `CameraModule` is omitted.
- [ ] Remove `CameraControllerRegistry*` from
      `AssetImportPipelineDependencies`,
      `RuntimeImportCompletedServices`, every dependency rebinding path, and
      every completed-service aggregate. The Sandbox completed handler must
      always register and must select the first valid created entity whenever
      scene/selection are available. Camera/config are optional only for the
      autofocus branch; their absence must not fail or roll back selection.
- [ ] Migrate Sandbox editor camera models/commands to
      `Services().Find<CameraControllerRegistry>()`. A missing registry means
      camera controls are unavailable and commands return the existing
      missing-camera status; model building and non-camera editor commands
      must not dereference or require the service.
- [ ] Reduce `Extrinsic.Runtime.ReferenceScene` to data-only population
      records plus these two behavior-bearing free functions:
      `BootstrapReferenceScene(selector, scene) -> ReferenceScenePopulation`
      and `TeardownReferenceScene(scene, population)`. Delete
      `IReferenceSceneProvider`, the public `TriangleProvider` type,
      `ReferenceSceneRegistry`, `MakeDefaultReferenceSceneRegistry`,
      `RegisterDefaultReferenceProvidersIfAbsent`,
      `BuildReferenceCameraViewInput`, and all provider registration/resolve
      implementation. Keep the triangle construction private behind the plain
      bootstrap function.
- [ ] Delete both `Runtime.ReferenceSceneControl` source files and their CMake
      registration. Sandbox retains one
      `{WorldHandle, ReferenceScenePopulation}` bootstrap record, creates it at
      most once per application initialization when reference content is
      enabled, and passes `population.Camera` through
      `SetWorldSeed(owningWorld, ...)` only when a camera registry exists.
      Teardown looks up and mutates only the stored original world; if that
      world has retired, teardown is a safe no-op and never targets the current
      replacement world. Reference content must bootstrap and render without a
      `CameraModule`.
- [ ] Remove all camera/reference state, imports, initialization, teardown,
      and `GetCameraControllerRegistry`, `GetReferenceSceneRegistry`,
      `IsReferenceSceneInstalled`, and `GetReferenceCameraSeed` facades from
      Engine. Ratchet the exact post-task convergence snapshot to
      `35` plain imports / `13` domain imports / `2` re-exports /
      `25` public getter names: the four removed Engine imports are
      `Extrinsic.Graphics.CameraSnapshots`,
      `Extrinsic.Runtime.CameraControllers`,
      `Extrinsic.Runtime.ReferenceScene`, and
      `Extrinsic.Runtime.ReferenceSceneControl`; the three counted getter
      removals are `GetCameraControllerRegistry`,
      `GetReferenceSceneRegistry`, and `GetReferenceCameraSeed`
      (`IsReferenceSceneInstalled` is removed but is not a counted `Get*`).

## Tests

- [ ] Add `tests/contract/runtime/Test.CameraModule.cpp` covering exact
      service publication/withdrawal, duplicate publication conflict,
      partial-register cleanup, shutdown/reinitialize, optional omission,
      finite composed first frame, disabled-then-hot-enabled seed use, capture
      suppression, and active controller/view behavior.
- [ ] Cover registry world semantics directly: reset clears every slot/pose,
      pending transition, and seed even for equal handle bits; wrong/unbound
      seed rejection leaves state unchanged; active-world change resets before
      the next hook; the hook repeats the handle check; away/back does not
      resurrect; world retirement invalidates; shutdown/reinitialize with
      recycled bits remains empty.
- [ ] Extend `Test.RuntimeModule.cpp` and
      `Test.RuntimeEnginePrivateGlue.cpp` for the typed registrar, empty-hook
      rejection, deterministic module-name ordering, schedule clearing,
      registration- and resolution-phase wiring, exact dispatch location, and
      proof that no seventh `FramePhase` or viewport fields entered
      `RuntimeFrameHookContext`.
- [ ] Rewrite `Test.RuntimeReferenceScene.cpp` around
      bootstrap/population/teardown free functions. Cover disabled/enabled
      Sandbox bootstrap, exactly once per initialization, teardown through the
      original world, retired-original-world no-op, no mutation of a
      replacement world, seed handoff, visible/selectable reference content,
      and content operation without `CameraModule`. Move projection/view
      assertions to controller/module tests and delete provider/registry
      lifecycle tests.
- [ ] Update `Test.RuntimeInputActions.cpp`,
      `Test.AssetImportFormatCoverage.cpp`,
      `Test.SandboxEditorVisualization.cpp`,
      `Test.SandboxEditorSceneCommands.cpp`,
      `Test.ImGuiAdapterEngineWiring.cpp`, and related fixtures/callers for
      optional registry lookup and the narrowed service aggregates. Prove the
      Sandbox `F` action is absent without camera, generic actions still run,
      editor models/commands report unavailable camera controls, and import
      selection succeeds without autofocus.
- [ ] Update `Test.RuntimeSandboxAcceptance.cpp` and
      `Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` callers for explicit
      Sandbox composition/reference bootstrap. Preserve first-frame camera,
      `F` focus, import autofocus/selection, reference-content visibility, and
      GPU smoke compilation/runtime behavior.
- [ ] Replace the obsolete reference-control Engine-layering expectations in
      `Test.RuntimeEngineLayering.cpp` with ratchets for no Engine camera or
      reference ownership/facades, the private camera module, the two-function
      reference seam, and the exact convergence counts.
- [ ] Refresh the test-gate routing baseline for the new
      `Test.CameraModule.cpp` producer and any renamed/rewritten test cases,
      then run focused camera/reference/module/input/import/Sandbox coverage,
      strict structural gates, the complete default CPU-supported gate, and
      the Vulkan-capable Sandbox smoke when available.

## Docs

- [ ] Mark ADR-0015 superseded by ADR-0027/RUNTIME-180 where it rejects
      app-owned bootstrap; update ADR-0027 and the runtime, graphics, config,
      kernel, input, import, Sandbox, and parity documentation with the typed
      hook, exact optional registry, world-bound camera state, app-owned
      initial-world population, and omission behavior.
- [ ] Update `src/runtime/README.md`, `src/app/README.md`,
      `src/app/Sandbox/README.md`, and live architecture/backlog summaries to
      remove provider/control/facade claims and record the composed module and
      plain reference bootstrap.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after adding
      `Extrinsic.Runtime.CameraModule` and deleting
      `Extrinsic.Runtime.ReferenceSceneControl`.

## Acceptance criteria

- [ ] Engine contains no camera-controller or reference-scene domain state,
      imports, initialization/teardown policy, or public compatibility facades.
- [ ] `FramePhase` still has exactly the six post-`RUNTIME-182` values and
      `RuntimeFrameHookContext` is unchanged; camera insertion uses only the
      dedicated typed viewport-input schedule.
- [ ] Camera update runs after the completed editor-capture snapshot and
      render-input initialization, and before gizmo, picking, and generic input
      actions.
- [ ] The exact published registry is bound to one valid world or invalid at
      shutdown; no reset, handle recycling, away/back transition, or world
      retirement can retain or resurrect a controller pose, transition, or
      seed.
- [ ] The module is optional: no service, update, or fallback camera appears
      when omitted, while generic input, editor non-camera behavior, import
      selection, and reference content remain operational.
- [ ] Generic Engine never interprets `ReferenceSceneConfig`; Sandbox owns
      exactly-once bootstrap and original-world teardown, and the only public
      reference-scene behavior surface is the two plain free functions.
- [ ] Sandbox camera focus behavior remains available when composed, while
      `F`, import autofocus, and editor camera controls fail closed or omit
      themselves when the exact registry is unavailable.
- [ ] The kernel convergence gate reports exactly
      `35 / 13 / 2 / 25` with no temporary debt, unused imports, or retained
      compatibility getter.
- [ ] Integration-labeled Engine/Sandbox execution demonstrates
      `Operational` camera-module composition; CPU contracts alone do not
      close the task.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'CameraModule|Camera|ReferenceScene|RuntimeModule|InputAction|AssetImport|ImGuiAdapter|SandboxEditor|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicTests
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Vulkan-capable/ci-vulkan lane: compile the migrated caller everywhere the
# preset is available; execute the gpu+vulkan smoke on a capable host.
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes

- Adding a seventh generic frame phase, widening
  `RuntimeFrameHookContext`, or routing camera through
  `BeforeExtraction`.
- Adding a camera service wrapper around `CameraControllerRegistry` or an
  Engine forwarding facade.
- Moving app-specific reference bootstrap/teardown policy back into Engine.
- Retaining Engine camera/reference compatibility getters, a
  `ReferenceSceneControl`, or any provider/registry compatibility re-export.
- Making import selection, generic input actions, or non-camera editor
  behavior depend on `CameraModule`.
- Keeping a per-world camera-state cache, restoring controller state on world
  revisit, or tearing a stored reference population down through a different
  world.
- Changing camera motion/projection semantics, adding another viewport, or
  broadening the task into scene-editing ownership.

## Maturity

- Target: `Operational`; the composed `CameraModule` must drive the existing
  runtime camera path during an integration-labeled Engine/Sandbox run.

---
id: RUNTIME-182
theme: F
depends_on:
  - ARCH-016
  - RUNTIME-181
maturity_target: Operational
---
# RUNTIME-182 — Extract the editor-UI composition module

## Goal
- Move the optional ImGui bridge, editor host, and window registry out of
  `Runtime.Engine` into one app-composed `EditorUiModule` that writes the
  frame-loop-owned capture value, while Sandbox panel content remains
  app-owned.

## Non-goals
- No new editor panel, docking behavior, input filter chain, render pass, or
  UI framework.
- No movement of Sandbox presentation back into runtime.
- No priority capture chain until a second production capture producer exists.
- No Engine callback or adapter compatibility facade.

## Context
- Owner/layer: runtime owns ImGui/runtime/renderer composition; app owns
  Sandbox presentation. Editor UI state is global and optional.
- The proven input contract is one end-of-editor-frame capture snapshot that
  gates viewport camera, gizmo, and picking input.
- Engine currently owns private `ImGuiEditorBridge` state while exposing
  callback, visibility, registration, and adapter-diagnostic facades.
- The existing frame bracket is load-bearing:
  `ImGuiAdapter::BeginFrame()` runs at the current pre-variable-tick callsite,
  `IApplication::OnVariableTick()` stays inside that bracket, app UI
  contributions run afterward, and `ImGuiAdapter::EndFrame()` plus capture
  finalization run at the current pre-viewport-input callsite. A single
  `UiBuild` callback that moves either end of this bracket is not equivalent.
- The generic optional owner depends only on exact kernel
  `Platform::IWindow`, `Graphics::IRenderer`, input-action, frame-hook, capture,
  and pacing capabilities. App-owned Sandbox composition resolves scene,
  camera, config, and asset services under `RUNTIME-168` and registers panels
  against the published editor host; those domain dependencies must not leak
  into this owner.
- The right-sized shape is one `EditorUiModule` PImpl. It absorbs and deletes
  the Engine-private bridge instead of wrapping it, publishes the existing
  generic host directly, and introduces no capture-provider abstraction.
- The checked post-`RUNTIME-181` Engine snapshot is 40 plain imports, 18
  domain imports, two re-exports, and 29 public getter names. This slice must
  ratchet that exact snapshot to 39/17/2/28.

## Status
- In progress as of 2026-07-19; owner: Codex team; branch:
  `codex/runtime-182-editor-ui-module`.
- Intake, lifecycle, omission, caller, and right-sizing audits are complete.
  Next gate: land the paired hook/context substrate and module lifecycle, then
  build focused CPU and conditionally enabled GPU callers.

## Required changes
- [ ] Define the three-boolean `EditorInputCaptureSnapshot` directly in
      `Runtime.Module` before the frame-hook context. Add ordered `UiBegin`,
      `UiBuild`, and `UiEndCapture` phases, and let each ephemeral hook context
      borrow the same capture value and `RuntimeFramePacingDiagnostics` by
      reference.
- [ ] Move the data-only capture type out of `Runtime.ImGuiAdapter` domain
      surface. Own one value in the frame loop for the full frame, clear it to
      unclaimed once at frame start, and let every ephemeral frame-hook context
      borrow it by reference. Invoke `UiBegin` at the current
      `BeginFrame` callsite, preserve `IApplication::OnVariableTick`, invoke
      `UiBuild`, then invoke `UiEndCapture` at the current
      `EndFrame`/capture callsite. Later viewport behavior and kernel input
      actions read that completed value.
- [ ] Publish exact `Platform::IWindow`, `Graphics::IRenderer`, and
      `RuntimeInputActionRegistry` instances as built-in kernel services before
      module registration. Re-publish fresh live instances on reinitialize and
      leave omission behavior unchanged.
- [ ] Add one concrete PImpl `EditorUiModule`. On registration it creates
      fresh boot state, publishes the exact Engine-free `EditorUiHost`, and
      registers the paired hooks; on resolution it requires only the three
      exact built-ins and initializes the existing adapter/overlay plus global
      `G` visibility action.
- [ ] Make `EditorUiHost` the narrow window/frame-contribution/visibility
      capability needed by app-owned content without storing or accepting
      `Engine&`. Keep the existing registry rather than adding a second
      registry or pass-through facade. The capture value travels through the
      hook context, not through the host service.
- [ ] Move ImGui timing and adapter-diagnostic copying out of
      `Runtime.FramePacingDiagnostics` and into the module implementation so
      the generic pacing module no longer imports the ImGui adapter.
- [ ] On reverse shutdown, unregister the exact visibility action, detach and
      shut down adapter/overlay while window/renderer are live, withdraw the
      exact host instance, clear all contributions, and destroy the boot PImpl.
      Reinitialize must not replay a stale callback, handle, capture, or
      visibility state.
- [ ] Migrate Sandbox to compose `EditorUiModule`, resolve the host during
      application attach, and explicitly register/unregister its frame
      contribution plus built-in/domain/method windows. `RUNTIME-168`
      separately owns construction of the domain-rich Sandbox session context
      from explicit module services.
- [ ] Remove Engine ImGui state/imports plus
      `SetImGuiEditorCallback`, `SetImGuiEditorVisible`, and
      `GetImGuiAdapter`; delete `Runtime.ImGuiEditorBridge.Internal.hpp` rather
      than retaining a second wrapper.
- [ ] Migrate every CPU and conditionally compiled GPU caller to explicit
      module/host composition and observation without an Engine compatibility
      facade.
- [ ] Ratchet the exact Engine convergence policy to 39 plain imports, 17
      domain imports, two re-exports, and 28 public getter names.

## Tests
- [ ] Preserve ImGui initialization, renderer overlay attachment, visibility,
      window registration, callback ordering, capture, and shutdown coverage.
- [ ] Add an ordering contract proving `UiBegin` precedes the application
      variable tick, registered panel mutations occur in `UiBuild`,
      `UiEndCapture` closes the pair, capture is written only after
      `EndFrame`, and every later viewport consumer sees that same completed
      value.
- [ ] Add an integration test proving the optional module can be omitted and a
      composed module executes one UI frame during `Engine::Run()`.
- [ ] Add lifecycle coverage for exact host publication/withdrawal,
      contribution and action cleanup, reverse shutdown while renderer/window
      are live, and fresh shutdown/reinitialize state.
- [ ] Compile `IntrinsicGraphicsVulkanSmokeTests` and
      `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` in the conditional
      Vulkan/GLFW configuration after migrating their ImGui callers. Execution
      is not required for this ownership-only slice.
- [ ] Update module-schedule, private-glue, Engine-layering,
      kernel-convergence, and test-gate-routing regressions for the new exact
      phases/surface.
- [ ] Run focused editor/ImGui/input/Sandbox coverage, strict layering, and the
      complete default CPU-supported gate.

## Docs
- [ ] Update runtime editor, Sandbox, and input-capture documentation with the
      module owner and single-snapshot contract.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Engine contains no ImGui/editor-host domain state, imports, callbacks, or
      adapter getters.
- [ ] Sandbox panels remain app-owned and consume runtime services only.
- [ ] `EditorUiModule` can compose without scene, camera, config, asset, or
      method owners.
- [ ] Viewport input is gated by exactly one proven capture snapshot; no unused
      filter framework is introduced.
- [ ] Omitting EditorUi leaves the kernel capture value unclaimed, and
      `Runtime.Engine.cppm` needs no ImGui/capture-domain import.
- [ ] The current
      `UiBegin → IApplication::OnVariableTick → UiBuild → UiEndCapture →
      viewport input` order is unchanged.
- [ ] Shutdown/reinitialize exposes no stale host, contribution, input action,
      capture, adapter diagnostics, visibility, renderer, or window reference.
- [ ] Strict kernel convergence reports exactly 39/17/2/28.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'EditorUiModule|EditorUiHost|ImGuiAdapter|SandboxEditor|InputAction|RuntimeModule|EngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding an editor facade on Engine or moving panels into runtime.
- Adding a priority input-filter registry for one capture producer.
- Introducing a second ImGui adapter, overlay, host, or window registry.
- Absorbing Sandbox default-policy, camera, scene-editing, asset-workflow, or
  explicit-application-lifecycle work owned by
  `RUNTIME-168`/`RUNTIME-180`/`RUNTIME-172`/`RUNTIME-183`/`RUNTIME-184`.
- Moving either end of the proven ImGui bracket around
  `IApplication::OnVariableTick`.

## Maturity
- Target: `Operational`; the optional module must run in the Sandbox
  integration path and retain omission/headless behavior.

---
id: RUNTIME-182
theme: F
depends_on:
  - ARCH-016
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
- The generic optional owner depends only on kernel window/renderer/hook
  capabilities. App-owned Sandbox composition resolves scene, camera, config,
  and asset services under `RUNTIME-168` and registers panels against the
  published editor host; those domain dependencies must not leak into this
  owner.

## Required changes
- [ ] Add one concrete `EditorUiModule` owning the existing ImGui bridge,
      `EditorUiHost`, editor-window registry, and visibility action; the module
      writes but does not own the frame-loop capture value.
- [ ] Resolve only window/renderer and kernel hook capabilities during module
      boot; do not store or pass `Engine&` or resolve Sandbox domain services.
- [ ] Register the existing UI build/bracketing and post-editor capture work at
      named frame hooks with the current ordering.
- [ ] Make the EditorUi `UiBuild` contribution preserve one indivisible
      `BeginFrame` → registered app panel build → `EndFrame` → capture-write
      order. The shared capture value is final before the phase returns and
      before any later camera/scene-editing hook or kernel input-action
      dispatch reads it.
- [ ] Move the data-only capture type out of `Runtime.ImGuiAdapter` domain
      surface. Own one value in the frame loop for the full frame, clear it to
      unclaimed once at frame start, and let every ephemeral frame-hook context
      borrow it by reference. The EditorUi `UiBuild` hook writes it; later
      behavior hooks and kernel input-action dispatch read the same value. Do
      not add a capture registry, provider callback, or Engine facade.
- [ ] Publish the narrow host/window-contribution capability needed by
      app-owned content without adding pass-through facades. The capture value
      travels through the kernel hook context, not a service.
- [ ] Migrate generic Sandbox editor callback/visibility registration to the
      host capability; `RUNTIME-168` separately owns construction of the
      domain-rich Sandbox session context from its explicit module services.
- [ ] Remove Engine ImGui state/imports plus
      `SetImGuiEditorCallback`, `SetImGuiEditorVisible`, and
      `GetImGuiAdapter`.

## Tests
- [ ] Preserve ImGui initialization, renderer overlay attachment, visibility,
      window registration, callback ordering, capture, and shutdown coverage.
- [ ] Add an ordering contract proving panel mutations occur inside the paired
      frame, capture is written only after `EndFrame`, and every later
      viewport consumer sees that same completed value.
- [ ] Add an integration test proving the optional module can be omitted and a
      composed module executes one UI frame during `Engine::Run()`.
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
- [ ] The current Begin/build/End/capture-before-viewport order is unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeIntegrationTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'EditorUiHost|ImGuiAdapter|SandboxEditor|InputAction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
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

## Maturity
- Target: `Operational`; the optional module must run in the Sandbox
  integration path and retain omission/headless behavior.

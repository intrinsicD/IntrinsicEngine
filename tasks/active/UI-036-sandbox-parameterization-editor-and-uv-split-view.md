---
id: UI-036
theme: I
depends_on: [RUNTIME-176, BUG-085]
maturity_target: Operational
---
# UI-036 — Sandbox parameterization editor panel and resizable UV split view

## Status
- Blocked on branch `codex/arch-006-completion`; owner: Codex.
- `RUNTIME-176` is retired at `CPUContracted`; its config, command, history,
  result, and pointer-free UV view-model seams are the only engine surfaces
  consumed here.
- One technical slice extends the existing app-owned
  `Sandbox.Editor.MethodPanels` module, adds pure presentation/action helpers
  for headless integration tests, synchronizes app/method docs, and then runs
  the default CPU plus live Vulkan Sandbox gates before retirement.
- Live Vulkan selection and LSCM execution exposed `BUG-085`: the promoted
  ImGui overlay drops draw-command clip rectangles, allowing the UV checker to
  escape its child pane. Resume retirement after that blocking seam is fixed
  and the live interaction is replayed.

## Goal
- Add the Sandbox editor window that lists only parameterization strategies
  implemented at landing, edits their typed parameters through the validated
  config path, applies the map, and shows the 2D UV layout in a resizable split
  view with checker/grid and aggregate distortion feedback.

## Non-goals
- No algorithm, runtime, or config code — the panel is presentation only and calls the `RUNTIME-176` facade/config surface and reads its `SandboxEditorParameterizationViewModel`; it never receives `Engine&` or owns geometry/runtime/asset state.
- No new control path that bypasses the config lane — the panel drives the same validated apply path an agent/config file uses.
- No second camera/viewport or renderer change — the UV layout is drawn as a 2D overlay with `ImGui`'s `ImDrawList` from the runtime view model (per `ADR-0025`, Option A); the GPU-rendered UV target is the optional follow-up `GRAPHICS-122`.
- No ImGui docking dependency — the split is a manual two-pane splitter inside one registered window, so no `ImGuiConfigFlags_DockingEnable`/`imgui.ini` persistence change is required.
- No direct UV-canvas pin picking, cut/weld, seam editing, or 3D gizmo. Current
  LSCM pin, harmonic custom-boundary, and BFF boundary arrays are edited as
  typed config values in the controls pane; later method tasks own any new
  payloads.
- No checker material creation or assignment. Parameterization writes
  `v:texcoord` and the existing runtime command marks it dirty, so an
  already-bound UV/checker material observes the update; material workflow is
  outside this presentation-only task.

## Context
- Owner/layer: `src/app/Sandbox/Editor/` (ImGui panels). `app -> runtime` only; panels import runtime seams, not lower layers.
- Panel pattern to mirror: `Sandbox.MethodPanels.cpp` for command routing and
  the progressive-Poisson panel for preview/apply through the config lane. Do
  not copy K-Means backend controls into this CPU-only surface.
- Window registration: `UI-034` `Runtime.EditorWindowRegistry` (decentralized `EditorWindowDescriptor` registration, lazy lifecycle, one input-capture snapshot) — register through it, not a central enum. Closed windows cost nothing.
- Split-view mechanism (per `ADR-0025`): one registered window split into a left **controls** pane and a right **UV layout** pane by a draggable `ImGui` splitter (child regions / `ImGui::Table` with a resizable column, or a stored split ratio with an invisible-button splitter). The UV pane maps the `SandboxEditorParameterizationViewModel` UVs into pane-local coordinates and draws triangle fills/edges via `ImGui::GetWindowDrawList()`; the runtime model does not invent chart/seam records or per-face distortion, and the adapter already forwards arbitrary draw lists, so this needs no renderer change.
- Interactive controls are generated only for real config alternatives. Current
  pin/boundary edits use the RUNTIME-176 validated apply path; future method
  tasks add ARAP/SLIM/SCP controls with their concrete payloads.
- The applied UVs are written back to the selected mesh by the `RUNTIME-176`
  facade (`v:texcoord` + dirty tag, undoable), so the UV pane and any
  already-bound UV/checker material observe the same source data.
- The app surface keeps a persistent draft and applies it explicitly through
  the validated config command. Per-widget hot apply is deliberately rejected:
  paired pin arrays and BFF target-angle data pass through temporarily invalid
  states while a user is editing them.

## Right-sizing
- **Element:** a new `ParameterizationPanel` module/class would be a one-window,
  one-caller surface beside the existing method-panel owner and is flagged by
  the single-consumer/pure-registration heuristics.
- **Simpler alternative:** register `mesh.processing.parameterize_uv` in the
  existing `Sandbox.Editor.MethodPanels` module. Keep ImGui and draft state in
  its current private implementation; add only plain app-owned value records
  and free functions for strategy inventory, projection, result summary, and
  the config-then-command action because those are shared by the window and
  integration tests.
- **Blast radius:** one existing app module interface/implementation, its
  existing integration-test target, app/method documentation, and generated
  module inventory. No new CMake target, runtime interface, renderer member,
  service, registry, queue, or backend edge.
- **Reintroduction trigger:** split a dedicated UV-view module only when a
  second independent app consumer or the optional `GRAPHICS-122` GPU view
  requires a reusable presentation interface.

## Control surfaces
- UI: `Mesh > Processing > Parameterize (UV)` window with the controls/UV split view.
- Config/Agent: unchanged from `RUNTIME-176` — the panel edits the same `EngineConfig.sandbox.parameterization` section and applies through the tagged `Editor` source, so config files and agents remain co-equal drivers.

## Required changes
- [ ] Register `mesh.processing.parameterize_uv` in the existing
      `Sandbox.Editor.MethodPanels` module, receiving `SandboxEditorContext`,
      not `Engine&`, with a draggable two-pane splitter (controls | UV layout)
      and a session-persistent panel split ratio.
- [ ] Strategy selector lists exactly the currently implemented
      LSCM/Harmonic-Cotangent/Tutte-Uniform/BFF tokens; do not add disabled
      future ARAP/SLIM/SCP or backend entries.
- [ ] Parameter widgets for each implemented strategy's actual config payload,
      edited through the RUNTIME-176 preview/apply path; no future-only knobs.
- [ ] UV layout pane: draw the `SandboxEditorParameterizationViewModel` as 2D
      triangle fills/edges over a toggleable unit-square grid/checker
      background, with fit-to-pane and zoom/pan. Show the aggregate diagnostics
      beside the view; do not synthesize chart/seam or per-face heatmap data.
- [ ] Interactive pin/boundary edits route through config apply; later method
      tasks own controls for their added payloads.
- [ ] Render chosen strategy, status, and the `ParameterizationDiagnostics`
      summary from `SandboxEditorParameterizationResult`.
- [ ] Apply/undo affordances routed through the runtime command so edits are undoable via `EditorCommandHistory`.
- [ ] Keep the config draft local until explicit apply; failed preview/apply
      preserves the draft and surfaces its diagnostic instead of mutating the
      active config or silently coercing a token.

## Tests
- [ ] Extend app/editor integration coverage to assert the parameterization
      window registers through the `UI-034` registry and the plain app action
      applies a typed config then invokes the configured runtime command without
      requiring ImGui frame state.
- [ ] Strategy gating: the panel cannot emit an unimplemented strategy token.
- [ ] View-model mapping: given a `SandboxEditorParameterizationViewModel`, the pane-space projection helper maps every UV inside the pane rect and preserves face count (pure model-level assertion, no ImGui frame).
- [ ] Result rendering surfaces status and diagnostics (model-level assertion).

## Docs
- [ ] Update the Sandbox editor UI inventory / user-facing sandbox docs with the parameterization window, the UV split view, and its config-lane parity note.
- [ ] Cross-link the parameterization method-package READMEs to the editor window as the interactive surface; reference `ADR-0025` for the derived-view rationale.

## Acceptance criteria
- [ ] Selecting a mesh, choosing an implemented strategy, tuning params, and
      applying updates `v:texcoord`, the UV layout pane, and any already-bound
      3D UV/checker material, undoably; this task does not assign a material.
- [ ] The controls/UV split is resizable via the draggable splitter; the
      grid/checker background toggles correctly and aggregate distortion
      diagnostics are visible.
- [ ] The panel drives the `RUNTIME-176` validated apply path (no private subsystem poke); config-file and agent drivers stay co-equal.
- [ ] Strategy status and aggregate distortion feedback are shown; unimplemented
      strategies are not offered.
- [ ] `app -> runtime` only; the panel owns no engine state.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|SandboxEditor|MethodPanel' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -R '^RuntimeSandboxAcceptanceGpuSmoke\\.ExtrinsicSandboxDefaultConfigProducesVisibleFrameWithValidation$' -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
Operational proof: the app integration test opens the registered window inside
`Engine::Run()`, drives the same plain config-then-command action on a selected
disk mesh, and verifies the produced ImGui layout plus UV/history state. On a
Vulkan-capable host, launch `ExtrinsicSandbox`, select a disk mesh, and
parameterize via `Mesh > Processing > Parameterize (UV)`; cite that run and the
existing operational Sandbox GPU acceptance smoke in the retirement note.

## Forbidden changes
- No `Engine&` in panel callbacks; no UI ownership of geometry/runtime/asset state.
- No private apply path that the config file/agent lane cannot reproduce.
- No offering an unimplemented strategy or placeholder backend selector.
- No enabling ImGui docking or writing `imgui.ini` in this task (the split is a manual splitter; docking is a separate opt-in if ever wanted).
- No new app panel-family/module or controller member for this single window;
  extend the existing method-panel owner.

## Maturity
- Target: `Operational` — the window drives the real runtime apply path in
  `Engine::Run()` and renders the UV layout. The integration-labeled app test
  covers registration, config-then-command execution, projection, result
  presentation, and a produced ImGui frame; the live Vulkan Sandbox run is
  cited separately. `GRAPHICS-122` remains only the optional dense-mesh GPU UV
  target, not a missing maturity gate for this CPU `ImDrawList` view.

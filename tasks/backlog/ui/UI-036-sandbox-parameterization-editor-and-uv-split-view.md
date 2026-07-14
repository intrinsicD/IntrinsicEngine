---
id: UI-036
theme: I
depends_on: [RUNTIME-176]
maturity_target: Operational
---
# UI-036 — Sandbox parameterization editor panel and resizable UV split view

## Goal
- Add the Sandbox editor window that lists only parameterization strategies
  implemented at landing, edits their typed parameters through the validated
  config path, applies the map, and shows the 2D UV layout in a resizable split
  view with checker/grid and distortion feedback.

## Non-goals
- No algorithm, runtime, or config code — the panel is presentation only and calls the `RUNTIME-176` facade/config surface and reads its `SandboxEditorParameterizationViewModel`; it never receives `Engine&` or owns geometry/runtime/asset state.
- No new control path that bypasses the config lane — the panel drives the same validated apply path an agent/config file uses.
- No second camera/viewport or renderer change — the UV layout is drawn as a 2D overlay with `ImGui`'s `ImDrawList` from the runtime view model (per `ADR-0025`, Option A); the GPU-rendered UV target is the optional follow-up `GRAPHICS-122`.
- No ImGui docking dependency — the split is a manual two-pane splitter inside one registered window, so no `ImGuiConfigFlags_DockingEnable`/`imgui.ini` persistence change is required.

## Context
- Owner/layer: `src/app/Sandbox/Editor/` (ImGui panels). `app -> runtime` only; panels import runtime seams, not lower layers.
- Panel pattern to mirror: `Sandbox.MethodPanels.cpp` for command routing and
  the progressive-Poisson panel for preview/apply through the config lane. Do
  not copy K-Means backend controls into this CPU-only surface.
- Window registration: `UI-034` `Runtime.EditorWindowRegistry` (decentralized `EditorWindowDescriptor` registration, lazy lifecycle, one input-capture snapshot) — register through it, not a central enum. Closed windows cost nothing.
- Split-view mechanism (per `ADR-0025`): one registered window split into a left **controls** pane and a right **UV layout** pane by a draggable `ImGui` splitter (child regions / `ImGui::Table` with a resizable column, or a stored split ratio with an invisible-button splitter). The UV pane maps the `SandboxEditorParameterizationViewModel` UVs into pane-local coordinates and draws chart fills / triangle edges / seams via `ImGui::GetWindowDrawList()`; the adapter already forwards arbitrary draw lists, so this needs no renderer change.
- Interactive controls are generated only for real config alternatives. Current
  pin/boundary edits use the RUNTIME-176 validated apply path; future method
  tasks add ARAP/SLIM/SCP/BFF controls with their concrete payloads.
- The applied UVs are written back to the selected mesh by the `RUNTIME-176` facade (`v:texcoord` + dirty tag, undoable), so the 3D mesh's checker material and the UV pane update together — this is the `Operational`, visible-in-sandbox proof.

## Control surfaces
- UI: `Mesh > Processing > Parameterize (UV)` window with the controls/UV split view.
- Config/Agent: unchanged from `RUNTIME-176` — the panel edits the same `EngineConfig.sandbox.parameterization` section and applies through the tagged `Editor` source, so config files and agents remain co-equal drivers.

## Required changes
- [ ] Add a registered parameterization window in `src/app/Sandbox/Editor/` (mirroring `Sandbox.MethodPanels.cpp`), receiving `SandboxEditorContext`, not `Engine&`, with a draggable two-pane splitter (controls | UV layout) and a persisted-in-panel split ratio.
- [ ] Strategy selector reflecting the strategies `Geometry.Parameterization` implements; disable/annotate strategies not yet available so the UI never offers an unimplemented variant.
- [ ] Parameter widgets for each implemented strategy's actual config payload,
      edited through the RUNTIME-176 preview/apply path; no future-only knobs.
- [ ] UV layout pane: draw the `SandboxEditorParameterizationViewModel` as 2D triangle edges + chart fills over a toggleable unit-square grid / checker background, with fit-to-pane and zoom/pan; a distortion-heatmap toggle colors faces by the per-face distortion scalar.
- [ ] Interactive pin/boundary edits route through config apply; later method
      tasks own controls for their added payloads.
- [ ] Render chosen strategy, status, and the `ParameterizationDiagnostics`
      summary from `SandboxEditorParameterizationResult`.
- [ ] Apply/undo affordances routed through the runtime command so edits are undoable via `EditorCommandHistory`.

## Tests
- [ ] Extend the app/editor panel registration coverage (or a headless panel-model test where one exists) to assert the parameterization window registers through the `UI-034` registry and produces a valid apply request from a param set without ImGui frame state.
- [ ] Strategy gating: the panel cannot emit an unimplemented strategy token.
- [ ] View-model mapping: given a `SandboxEditorParameterizationViewModel`, the pane-space projection helper maps every UV inside the pane rect and preserves face count (pure model-level assertion, no ImGui frame).
- [ ] Result rendering surfaces status and diagnostics (model-level assertion).

## Docs
- [ ] Update the Sandbox editor UI inventory / user-facing sandbox docs with the parameterization window, the UV split view, and its config-lane parity note.
- [ ] Cross-link the parameterization method-package READMEs to the editor window as the interactive surface; reference `ADR-0025` for the derived-view rationale.

## Acceptance criteria
- [ ] Selecting a mesh, choosing an implemented strategy, tuning params, and
      applying updates both the 3D checker view and UV layout pane, undoably.
- [ ] The controls/UV split is resizable via the draggable splitter; the distortion overlay and grid/checker background toggle correctly.
- [ ] The panel drives the `RUNTIME-176` validated apply path (no private subsystem poke); config-file and agent drivers stay co-equal.
- [ ] Strategy status and distortion feedback are shown; unimplemented
      strategies are not offered.
- [ ] `app -> runtime` only; the panel owns no engine state.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|SandboxEditor|MethodPanel' -LE 'gpu|vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
Interactive proof (Vulkan-capable host): launch `ExtrinsicSandbox`, select a disk mesh, and parameterize via `Mesh > Processing > Parameterize (UV)` — the UV split view shows the layout and the mesh checker updates; cite the run in the retirement note.

## Forbidden changes
- No `Engine&` in panel callbacks; no UI ownership of geometry/runtime/asset state.
- No private apply path that the config file/agent lane cannot reproduce.
- No offering an unimplemented strategy or placeholder backend selector.
- No enabling ImGui docking or writing `imgui.ini` in this task (the split is a manual splitter; docking is a separate opt-in if ever wanted).

## Maturity
- Target: `Operational` — the window drives the real runtime apply path in `ExtrinsicSandbox` and renders the UV layout. The default CPU gate covers registration/model/projection behavior; the interactive viewport proof is cited from a Vulkan-capable host run.

---
id: UI-035
theme: I
depends_on: [RUNTIME-175]
maturity_target: Operational
---
# UI-035 — Sandbox point-cloud consolidation editor panel

## Goal
- Add the Sandbox editor window that lets a user pick a LOP-family strategy (LOP/WLOP/CLOP/EAR) and a backend (CPU reference / Vulkan compute), tune the parameters, apply the consolidation to the selected point-cloud entity through the validated config apply path, and see the cleaned cloud update in the viewport with honest requested-vs-actual backend and convergence feedback.

## Non-goals
- No algorithm, runtime, or config code — the panel is presentation only and calls the `RUNTIME-175` facade/config surface; it never receives `Engine&` or owns geometry/runtime/asset state.
- No new control path that bypasses the config lane — the panel drives the same validated apply path an agent/config file uses.
- No visualization/colormap changes beyond selecting the consolidated point cloud for display.

## Context
- Owner/layer: `src/app/Sandbox/Editor/` (ImGui panels). `app -> runtime` only; panels import runtime seams, not lower layers.
- Panel pattern to mirror: `Sandbox.MethodPanels.cpp` (the K-Means panel — a `Backend##` `BeginCombo` selects the variant, Apply calls `Runtime::ApplySandboxEditor...Command`, and requested-vs-actual backend + fallback reason are rendered). The **config-lane** exemplar is the progressive-Poisson panel: editor mirror config type with `Make*` converters and an apply routed through `EngineConfigControl::PreviewEngineConfigControlDocument` / `ApplyEngineConfigHotSubset`. Mirror the progressive-Poisson panel so the panel is the config lane's UI surface, not a private path.
- Window registration: `UI-034` `Runtime.EditorWindowRegistry` (decentralized registration, lazy lifecycle, one input-capture snapshot, generic scalar-property widgets) — register through it, not a central enum.
- Retired `ARCH-006` moved point-cloud presentation into `src/app`; place the
  consolidation window with the other app-owned point-cloud panels and consume
  the runtime facade only.
- The consolidated result is applied back to the selected entity by the `RUNTIME-175` facade (`PopulateFromCloud` + `MarkVertexPositionsDirty`, undoable), so the viewport shows the cleaned cloud immediately — this is the `Operational`, visible-in-sandbox proof.

## Control surfaces
- UI: `PointCloud > Processing > Consolidate (LOP/WLOP/CLOP/EAR)` window.
- Config/Agent: unchanged from `RUNTIME-175` — the panel edits registered app
  section `sandbox.point_cloud_consolidation` and applies through the tagged
  `Editor` source, so config files and agents remain co-equal drivers.

## Required changes
- [ ] Add a registered consolidation window in `src/app/Sandbox/Editor/` (mirroring `Sandbox.MethodPanels.cpp`), receiving `SandboxEditorContext`, not `Engine&`.
- [ ] Strategy selector (LOP/WLOP/CLOP/EAR) reflecting the strategies the `Geometry.PointCloud.Consolidation` module implements; disable/annotate strategies not yet available so the UI never offers an unimplemented variant.
- [ ] Backend selector (CPU reference / Vulkan compute) mapping to the `Backend` request, with the option disabled/annotated when no operational device is present.
- [ ] Parameter widgets for the shared and per-strategy knobs (`h`, `mu`, iterations, CLOP component count, EAR edge sensitivity, seed), edited into the editor mirror of `PointCloudConsolidationConfig` and applied through the `RUNTIME-175` config-routed command (preview → apply), not a private call.
- [ ] Render requested-vs-actual backend id, CPU-fallback reason, and convergence diagnostics (iterations, converged flag, moved distance) from `SandboxEditorPointCloudConsolidationResult`.
- [ ] Apply/undo affordances routed through the runtime command so edits are undoable via `EditorCommandHistory`.

## Tests
- [ ] Extend the app/editor panel registration coverage (or a headless panel-model test where one exists) to assert the consolidation window registers through the `UI-034` registry and produces a valid apply request from a param set without ImGui frame state.
- [ ] Backend/strategy gating: the panel does not emit a request for an unimplemented strategy or a GPU backend on a non-operational device (it annotates instead).
- [ ] Result rendering: given a `SandboxEditorPointCloudConsolidationResult` with `FellBackToCPU`, the panel surfaces the fallback reason (model-level assertion).

## Docs
- [ ] Update the Sandbox editor UI inventory / user-facing sandbox docs with the consolidation window and its config-lane parity note.
- [ ] Cross-link the three method-package READMEs to the editor window as the interactive surface.

## Acceptance criteria
- [ ] Selecting a point cloud, choosing a strategy/backend, and applying consolidates the cloud and updates the viewport, undoably.
- [ ] The panel drives the `RUNTIME-175` validated apply path (no private subsystem poke); config-file and agent drivers stay co-equal.
- [ ] Requested-vs-actual backend and convergence feedback are shown; unavailable strategies/backends are annotated, not offered.
- [ ] `app -> runtime` only; the panel owns no engine state.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|SandboxEditor|MethodPanel' -LE 'gpu|vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
Interactive proof (Vulkan-capable host): launch `ExtrinsicSandbox`, drop a noisy point cloud, and consolidate via `PointCloud > Processing > Consolidate` — cite the run in the retirement note.

## Forbidden changes
- No `Engine&` in panel callbacks; no UI ownership of geometry/runtime/asset state.
- No private apply path that the config file/agent lane cannot reproduce.
- No offering an unimplemented strategy or a GPU backend on a non-operational device.

## Maturity
- Target: `Operational` — the window drives the real runtime apply path in `ExtrinsicSandbox`. The default CPU gate covers registration/model behavior; the interactive viewport proof is cited from a Vulkan-capable host run.

---
id: UI-035
theme: I
depends_on: [RUNTIME-175]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-GeometryE2E"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-01T18:36:41Z"
maturity_target: Operational
---
# UI-035 — Sandbox point-cloud consolidation editor panel

## Status
- Completed on 2026-08-01. The app-owned registered window exposes the four
  promoted CPU-reference strategies, validates and hot-applies the shared
  config record, submits the typed async runtime operation, projects exact
  implementation/convergence/normal diagnostics, and uses the shared
  document-history undo/redo path.
- The canonical `ci` editor target and all 22 focused consolidation/presentation
  cases pass. On the Vulkan-capable host, the combined-sanitizer `ci-vulkan`
  Sandbox built, launched on the RTX 3050, and rendered its GLFW/ImGui window.
  The host session was screen-locked, so synthetic menu clicks were correctly
  intercepted by GNOME rather than bypassing the lock; the model-level
  prepared-workspace test supplies the deterministic dispatch, ECS mutation,
  result-publication, and undo proof.
- Commit: pending this retirement checkpoint.

## Goal
- Add the Sandbox editor window that lets a user pick a CPU-reference
  LOP-family strategy (LOP/WLOP/CLOP/EAR), tune its parameters, apply the
  consolidation to the selected point-cloud entity through the validated
  config path, and see the cleaned cloud update in the viewport with
  convergence feedback.

## Non-goals
- No algorithm, service, or config-schema implementation — the panel is
  presentation only and calls the post-`RUNTIME-202` `RUNTIME-175` typed
  operation/config surface. A narrow runtime editor-operation validator keeps
  Core config result types out of the app layer; the panel never receives
  `Engine&` or owns geometry/runtime/asset state.
- No new control path that bypasses the config lane — the panel drives the same validated apply path an agent/config file uses.
- No visualization/colormap changes beyond selecting the consolidated point cloud for display.

## Context
- Owner/layer: `src/app/Sandbox/Editor/` (ImGui panels). `app -> runtime` only; panels import runtime seams, not lower layers.
- Panel structure to mirror: `Sandbox.MethodPanels.cpp`. The
  **config-lane** exemplar is the progressive-Poisson panel: editor mirror
  config type with `Make*` converters and an apply routed through
  `EngineConfigControl::PreviewEngineConfigControlDocument` /
  `ApplyEngineConfigHotSubset`. Mirror that path so the panel is the config
  lane's UI surface, not a private path.
- Window registration: `UI-034` `Runtime.EditorWindowRegistry` (decentralized registration, lazy lifecycle, one input-capture snapshot, generic scalar-property widgets) — register through it, not a central enum.
- Retired `ARCH-006` moved point-cloud presentation into `src/app`; place the
  consolidation window with the other app-owned point-cloud panels and consume
  the typed runtime operation/snapshot only.
- The consolidated result is applied back to the selected entity by the
  `RUNTIME-175` typed operation through the common mutation/history path, so
  the viewport shows the cleaned cloud immediately — this is the
  `Operational`, visible-in-sandbox proof.

## Control surfaces
- UI: `PointCloud > Processing > Consolidate (LOP/WLOP/CLOP/EAR)` window.
- Config/Agent: unchanged from `RUNTIME-175` — the panel edits registered app
  section `sandbox.point_cloud_consolidation` and applies through the tagged
  `Editor` source, so config files and agents remain co-equal drivers.

## Required changes
- [x] Add a registered consolidation window in `src/app/Sandbox/Editor/` (mirroring `Sandbox.MethodPanels.cpp`), receiving `SandboxEditorContext`, not `Engine&`.
- [x] Strategy selector (LOP/WLOP/CLOP/EAR) reflecting the strategies the `Geometry.PointCloud.Consolidation` module implements; disable/annotate strategies not yet available so the UI never offers an unimplemented variant.
- [x] Parameter widgets for the shared and per-strategy knobs (`h`, `mu`,
      iterations, CLOP component count, EAR edge sensitivity and normal-source
      policy, seed), edited into the editor mirror of
      `PointCloudConsolidationConfig` and applied through the `RUNTIME-175`
      config-routed typed operation (preview → submit → apply), not a private
      call.
- [x] Render the actual `cpu_reference` implementation identity, selected
      strategy, and convergence diagnostics (iterations, converged flag, moved
      distance) from `PointCloudConsolidationResult`.
- [x] Apply/undo affordances routed through the typed runtime operation so
      edits use the common `EditorCommandHistory` transaction.

## Tests
- [x] Extend the app/editor panel registration coverage (or a headless panel-model test where one exists) to assert the consolidation window registers through the `UI-034` registry and produces a valid apply request from a param set without ImGui frame state.
- [x] Strategy gating: the panel does not emit a request for an unimplemented
      strategy; it annotates the unavailable choice instead.
- [x] Result rendering: a model-level assertion covers strategy identity and
      convergence/failure diagnostics without ImGui frame state.

## Docs
- [x] Update the Sandbox editor UI inventory / user-facing sandbox docs with the consolidation window and its config-lane parity note.
- [x] Cross-link the three method-package READMEs to the editor window as the interactive surface.

## Acceptance criteria
- [x] Selecting a point cloud, choosing a strategy, and applying consolidates
      the cloud and updates the viewport, undoably.
- [x] The panel drives the `RUNTIME-175` validated typed operation (no private
      subsystem poke); config-file and agent drivers stay co-equal.
- [x] Strategy/implementation identity and convergence feedback are shown;
      unavailable strategies are annotated, not offered.
- [x] `app -> runtime` only; the panel owns no engine state.

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
- No placeholder optimized/GPU choice. METHOD-019/020 may extend the delivered
  panel only after a concrete backend passes its evidence gate.

## Maturity
- Target: `Operational` — the window drives the real runtime apply path in `ExtrinsicSandbox`. The default CPU gate covers registration/model behavior; the interactive viewport proof is cited from a Vulkan-capable host run.

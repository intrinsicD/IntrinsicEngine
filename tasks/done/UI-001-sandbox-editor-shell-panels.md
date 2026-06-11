# UI-001 — Sandbox editor shell and core panels

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-03.
- Final implementation commit: this retirement commit.
- Maturity: `CPUContracted`.
- Summary: Slices A-D are complete. The promoted sandbox editor shell now
  exposes the required scene hierarchy, inspector, selection/refined-primitive
  details, file/import entry, camera/render settings, spatial-debug,
  visualization-config, and visualization adapter-binding controls. Commands
  route through runtime-owned seams, including Slice D's
  `Engine::ImportAssetFromPath(...)` file/import command over the retired
  `ASSETIO-001` asset route/model/texture ingest and handoff surfaces.
  RUNTIME-095 closes the scoped visual/interactive proof; broad file-backed UI
  workflows remain future work.

## Slice plan
- **Slice A.** Add the promoted runtime sandbox editor UI module,
  panel model builders, deterministic disabled file/import diagnostics, scene
  hierarchy selection command emission, and sandbox attachment through
  `Engine::SetImGuiEditorCallback`. Add CPU `contract;runtime` coverage for the
  model builders and callback registration. Defers fully interactive
  camera/render setting mutation, visualization adapter command routing, asset
  import execution, and final visual proof to later slices.
- **Slice B.** Extend the inspector and selection details panels to
  cover the runtime/editor-facing data surface available after Slice A:
  transform rotation/scale/position, render hint parameters, selected/hovered
  entity rows, refined primitive kind/id/hit data, and a runtime-owned local
  transform edit command that stamps `Transform::IsDirtyTag`. Defers asset
  import execution, camera/render settings beyond local transform edits, and
  visualization adapter routing.
- **Slice C.1.** Add runtime-owned command seams for active
  camera-controller replacement and the legacy mesh edge/vertex primitive-view
  toggle command surface. These commands route through
  `Engine::GetCameraControllerRegistry()` and
  `Engine::{Set,Get,Clear}MeshPrimitiveViewSettings(...)`, so UI state remains
  presentation-only; RUNTIME-106 later made those mesh view methods translate to
  `RenderEdges` / `RenderPoints`.
- **Slice C.2.** Add the runtime-owned command seams for
  selected-entity spatial-debug settings and visualization-config selection
  without making UI state authoritative. The slice routes through
  `ECS::Components::SpatialDebugBinding` and
  `Graphics::Components::VisualizationConfig`; non-scalar adapter packet
  extraction is now available through retired `RUNTIME-083`.
- **Slice C.3.** Add a runtime-owned command/model seam for selected-entity
  visualization adapter bindings. The slice routes through
  `Engine::{Set,Get,Clear}VisualizationAdapterBinding(...)` and the
  `RenderExtractionCache::VisualizationAdapterBinding` control surface so UI
  state remains presentation-only and adapter validation stays in
  `VisualizationAdapters` / render extraction.
- **Slice D.** Add the runtime-owned file/import command seam and close
  `CPUContracted` for all required panels. The panel forwards path/payload hints
  to `Engine::ImportAssetFromPath(...)`, folds success/failure diagnostics back
  into the model, and leaves final scoped `Operational` proof to `RUNTIME-095`.

## Goal
- Implement the first promoted sandbox/editor UI shell on top of the runtime ImGui adapter, with core panels for scene hierarchy, file/import entry points, inspector, selection details, camera/render settings, and geometry-domain visualization controls.

## Non-goals
- No Dear ImGui platform/renderer adapter plumbing (`RUNTIME-090`) and no graphics ImGui pass wiring (`GRAPHICS-079`).
- No asset ingest implementation (`ASSETIO-001`) or GPU residency implementation (`RUNTIME-085..087`, `GRAPHICS-034`); UI composes promoted runtime/asset commands only.
- No geometry algorithm implementation; panels invoke registered runtime/method commands only.
- No persistent scene serializer/prefab workflow.
- No UI-owned simulation, render, asset, or selection authority; UI emits commands/events to owning runtime systems.

## Context
- Owner/layer: `ui` / runtime-editor integration. The app layer stays policy-light; sandbox UI should attach through runtime/editor hooks, not direct graphics ownership.
- Upstream dependencies: `RUNTIME-090` must produce ImGui frames; `GRAPHICS-079` must render them; `RUNTIME-089` supplies selection state; retired `RUNTIME-083` supplies visualization adapter seams; retired `ASSETIO-001` supplies the promoted file/import routing, model/texture ingest, and runtime handoff seams that Slice D should compose.
- Existing `tasks/backlog/ui/RORG-031-ui-integration.md` is a seed only. This task scopes the first concrete panel set needed for the working sandbox path.

## Required changes
- [x] Add a promoted sandbox/editor UI module that registers a draw callback with the runtime ImGui adapter without adding app-layer graphics/runtime shortcuts.
- [x] Implement a scene hierarchy panel that lists live scene entities by `MetaData::EntityName` and selection state, and emits selection commands through `RUNTIME-089` APIs.
- [x] Implement an inspector panel that shows transform, stable ID/metadata, render hints (`RenderSurface`/`RenderEdges`/`RenderPoints`), and available `GeometrySources` domains read through runtime/editor-facing views.
- [x] Implement a selection/primitive details panel that displays current entity/face/edge/vertex/point selection results from `RUNTIME-089` / `RUNTIME-093`. Slice B models selected/hovered entity rows and refined primitive status/domain/kind/id/hit details; richer interaction remains for later command-surface slices.
- [x] Implement a file/import entry panel that calls asset/runtime import commands when promoted asset/runtime ingest is available and otherwise displays a deterministic disabled-state diagnostic. Slice A implements the deterministic disabled diagnostic; Slice D routes command execution through `Engine::ImportAssetFromPath(...)` on top of the retired `ASSETIO-001` runtime/asset seams.
- [x] Implement camera/render settings controls for active camera controller selection, debug overlay toggles, and primitive view toggles using runtime-owned APIs. Slice C.1 implements active camera-controller replacement and mesh edge/vertex primitive-view toggles; Slice C.2 implements selected-entity spatial-debug binding controls.
- [x] Implement geometry-domain visualization controls that choose scalar/color/vector/isolines as inputs to `RUNTIME-083` adapters without duplicating graphics validation. Slice C.2 implements selected-entity `VisualizationConfig` material/scalar/color command routing; Slice C.3 implements runtime-owned visualization adapter-binding command/model routing through the engine-owned `RenderExtractionCache`. Retired `RUNTIME-083` provides scalar/color/vector/isoline/Htex adapter contracts plus scalar and non-scalar extraction selection.
- [x] Add UI diagnostics for missing dependencies (no ImGui adapter, no asset ingest, no selected entity, unsupported domain).

## Tests
- [x] Add `contract;runtime` or `unit;ui` coverage for panel model builders independent of Dear ImGui draw calls where possible.
- [x] Add a `contract;runtime` test with a Null platform/window that registers the UI draw callback and produces a deterministic empty/disabled panel frame through `RUNTIME-090`.
- [x] Add tests for scene hierarchy selection command emission, inspector domain enumeration, and disabled file/import state before `ASSETIO-001` lands.
- [x] Add tests for selected/hovered entity detail models, refined primitive id/hit detail models, render-hint parameters, and local transform edit command dirty tagging.
- [x] Add tests for runtime-owned camera-controller replacement and mesh edge/vertex primitive-view toggle command routing.
- [x] Add tests for selected-entity spatial-debug binding and visualization-config command routing.
- [x] Add tests for selected-entity visualization adapter-binding command/model routing through runtime extraction state.
- [x] Add tests for file/import command routing through a runtime-owned surface and the engine import facade's missing-file diagnostics.
- [x] Add test labels per `tests/README.md`; add a new `ui` label to `tests/README.md` and `tests/CMakeLists.txt` only if it does not already exist.
- [x] No `gpu`/`vulkan` test in this slice; visual overlay proof is owned by final sandbox acceptance.

## Docs
- [x] Update `tasks/backlog/ui/README.md` with UI-001 and its dependency gates.
- [x] Update runtime/editor docs (`src/runtime/README.md` or a UI README if added) with the UI ownership model and command/event boundaries.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` rows for the promoted sandbox/editor UI panels.
- [x] Update `src/app/Sandbox/README.md` and the working-sandbox gate review with Slice B's runtime-owned transform edit seam, Slice C.1's camera/primitive-view command seams, Slice C.2's spatial-debug/visualization-config seams, Slice C.3's visualization adapter-binding seam, Slice D's file/import command seam, and the downstream `RUNTIME-095` operational proof.
- [x] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [x] The sandbox has a promoted ImGui-based editor shell once `RUNTIME-090` and `GRAPHICS-079` are present.
- [x] Core panels expose scene hierarchy, inspector, selection details, file/import entry points, camera/render settings, and visualization controls with deterministic disabled states for missing backends.
- [x] UI emits commands/events to runtime/editor owners and does not own engine state directly. Slice A emits selection commands through `SelectionController`; Slice B applies local transform edit commands through a runtime-owned seam and stamps `Transform::IsDirtyTag`; Slice C.1 replaces camera-controller slots and toggles mesh primitive views through runtime-owned engine/extraction seams; Slice C.2 applies selected-entity spatial-debug and visualization-config commands through runtime-owned ECS/graphics data components; Slice C.3 routes selected-entity visualization adapter bindings through engine-owned render-extraction state; Slice D routes file/import execution through `Engine::ImportAssetFromPath(...)` and the retired ASSETIO runtime/asset seams.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'runtime' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Importing graphics/RHI ownership into UI panels.
- Making `Sandbox::App` a special gameplay/demo layer to bypass runtime composition.
- Implementing asset ingest, geometry residency, selection refinement, or ImGui backend plumbing inside panel code.
- Adding UI state that is authoritative over simulation, assets, or rendering.

## Maturity
- Slice A closes `CPUContracted` for the promoted shell, panel model/callback
  coverage, selection command emission, and deterministic disabled states.
  Slice B closes `CPUContracted` for enriched inspector/selection detail models,
  refined primitive id/hit display, and local transform edit command dirty
  tagging. Slice C.1 closes `CPUContracted` for camera-controller replacement
  and mesh primitive-view toggle command routing. Slice C.2 closes
  `CPUContracted` for selected-entity spatial-debug binding and
  visualization-config command routing. Slice C.3 closes `CPUContracted` for
  selected-entity visualization adapter-binding command/model routing. Slice D
  closes `CPUContracted` for file/import execution on top of the retired
  ASSETIO runtime/asset seams and retires the full task.
- Scoped `Operational` proof closed by
  [`RUNTIME-095`](RUNTIME-095-working-sandbox-acceptance.md) after ImGui rendering
  is wired.

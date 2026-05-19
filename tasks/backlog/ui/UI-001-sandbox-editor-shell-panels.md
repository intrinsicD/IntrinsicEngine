# UI-001 — Sandbox editor shell and core panels

## Goal
- Implement the first promoted sandbox/editor UI shell on top of the runtime ImGui adapter, with core panels for scene hierarchy, file/import entry points, inspector, selection details, camera/render settings, and geometry-domain visualization controls.

## Non-goals
- No Dear ImGui platform/renderer adapter plumbing (`RUNTIME-090`) and no graphics ImGui pass wiring (`GRAPHICS-079`).
- No asset ingest implementation (`ASSETIO-001`) or GPU residency implementation (`RUNTIME-085..087`, `GRAPHICS-034`).
- No geometry algorithm implementation; panels invoke registered runtime/method commands only.
- No persistent scene serializer/prefab workflow.
- No UI-owned simulation, render, asset, or selection authority; UI emits commands/events to owning runtime systems.

## Context
- Owner/layer: `ui` / runtime-editor integration. The app layer stays policy-light; sandbox UI should attach through runtime/editor hooks, not direct graphics ownership.
- Upstream dependencies: `RUNTIME-090` must produce ImGui frames; `GRAPHICS-079` must render them; `RUNTIME-089` supplies selection state; `RUNTIME-083` supplies visualization adapter seams; `ASSETIO-001` supplies file/import routing once implemented.
- Existing `tasks/backlog/ui/RORG-031-ui-integration.md` is a seed only. This task scopes the first concrete panel set needed for the working sandbox path.

## Required changes
- [ ] Add a promoted sandbox/editor UI module that registers a draw callback with the runtime ImGui adapter without adding app-layer graphics/runtime shortcuts.
- [ ] Implement a scene hierarchy panel that lists live scene entities by `MetaData::EntityName` and selection state, and emits selection commands through `RUNTIME-089` APIs.
- [ ] Implement an inspector panel that shows transform, stable ID/metadata, render hints (`RenderSurface`/`RenderLines`/`RenderPoints`), and available `GeometrySources` domains read through runtime/editor-facing views.
- [ ] Implement a selection/primitive details panel that displays current entity/face/edge/vertex/point selection results from `RUNTIME-089` / `RUNTIME-093`.
- [ ] Implement a file/import entry panel that calls asset/runtime import commands when `ASSETIO-001` is available and otherwise displays a deterministic disabled-state diagnostic.
- [ ] Implement camera/render settings controls for active camera controller selection, debug overlay toggles, and primitive view toggles using runtime-owned APIs.
- [ ] Implement geometry-domain visualization controls that choose scalar/color/vector/isolines as inputs to `RUNTIME-083` adapters without duplicating graphics validation.
- [ ] Add UI diagnostics for missing dependencies (no ImGui adapter, no asset ingest, no selected entity, unsupported domain).

## Tests
- [ ] Add `contract;runtime` or `unit;ui` coverage for panel model builders independent of Dear ImGui draw calls where possible.
- [ ] Add a `contract;runtime` test with a Null platform/window that registers the UI draw callback and produces a deterministic empty/disabled panel frame through `RUNTIME-090`.
- [ ] Add tests for scene hierarchy selection command emission, inspector domain enumeration, and disabled file/import state before `ASSETIO-001` lands.
- [ ] Add test labels per `tests/README.md`; add a new `ui` label to `tests/README.md` and `tests/CMakeLists.txt` only if it does not already exist.
- [ ] No `gpu`/`vulkan` test in this slice; visual overlay proof is owned by final sandbox acceptance.

## Docs
- [ ] Update `tasks/backlog/ui/README.md` with UI-001 and its dependency gates.
- [ ] Update runtime/editor docs (`src/runtime/README.md` or a UI README if added) with the UI ownership model and command/event boundaries.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` rows for the promoted sandbox/editor UI panels.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new modules are added.

## Acceptance criteria
- [ ] The sandbox has a promoted ImGui-based editor shell once `RUNTIME-090` and `GRAPHICS-079` are present.
- [ ] Core panels expose scene hierarchy, inspector, selection details, file/import entry points, camera/render settings, and visualization controls with deterministic disabled states for missing backends.
- [ ] UI emits commands/events to runtime/editor owners and does not own engine state directly.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract;runtime|ui' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
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
- Target: `CPUContracted` panel model/callback coverage plus deterministic disabled states.
- `Operational` visual proof is owned by final sandbox acceptance after ImGui rendering is wired.


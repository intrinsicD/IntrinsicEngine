# UI-002 — Sandbox EditorUI domain menu windows

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: `SandboxEditorUi` now draws promoted `PointCloud`, `Graph`, and
  `Mesh` ImGui menu slots whose submenu items open persistent selected-entity
  domain windows. The domain-window models report selected mesh/graph/cloud
  state plus no-selection, stale-selection, missing-dependency, and wrong-domain
  disabled diagnostics; mutation continues through existing runtime-owned
  command seams rather than UI-owned state.

## Goal
- Add an ImGui menu bar to the promoted sandbox EditorUI with dedicated
  `PointCloud`, `Graph`, and `Mesh` menu slots whose submenu items open
  domain-specific windows for inspecting and controlling the selected entity's
  runtime-owned ECS/render components.

## Non-goals
- No legacy `src/legacy/EditorUI` revival or dependency on legacy editor
  controllers.
- No UI-owned simulation, asset, selection, render, or geometry authority.
- No new geometry algorithms or method integrations.
- No persistence of editor layout/state to disk.
- No requirement to implement a complete DCC-style property editor in this
  slice; the task should expose the selected domain's existing runtime command
  seams first.

## Context
- Owner/layer: `ui` / promoted runtime editor integration in
  `src/runtime/Editor/Runtime.SandboxEditorUi.*`.
- `src/app/Sandbox/Sandbox.cppm` currently attaches `SandboxEditorUi` and must
  remain a runtime-only consumer.
- `SelectionController` in
  `src/runtime/Runtime.SelectionController.cppm` is the selected/hovered entity
  authority. Domain windows must consume `SandboxEditorContext`/panel models and
  emit commands through runtime-owned seams rather than caching authoritative
  component state in UI.
- Existing `SandboxEditorUi` windows expose hierarchy, inspector, selection,
  file/import, camera/render, and visualization panels directly. This task adds
  the requested menu-opened domain windows on top of that shell, using
  `PointCloud`, `Graph`, and `Mesh` as first-class menu slots.
- `UI-001`, `RUNTIME-085..089`, `RUNTIME-092`, `RUNTIME-093`, and
  `RUNTIME-095` are retired and provide the current selection, residency,
  primitive-refinement, and panel-model surfaces this task should reuse.

## Required changes
- [x] Add a promoted ImGui main-menu path in `SandboxEditorUi` with stable
      top-level `Mesh`, `Graph`, and `PointCloud` menus.
- [x] Add submenu items under each domain that toggle persistent ImGui windows
      for the current selected entity, with deterministic disabled states when
      no entity is selected or the selected entity has another
      `GeometrySources` domain.
- [x] Implement mesh-domain windows for at least surface render hint status,
      mesh primitive edge/vertex view toggles, visualization config, spatial
      debug binding, and current primitive selection details.
- [x] Implement graph-domain windows for line/point render hint status,
      visualization config, spatial debug binding, and graph primitive
      selection details.
- [x] Implement point-cloud-domain windows for point render hint status,
      visualization config, spatial debug binding, and point selection details.
- [x] Add runtime-owned command helpers as needed for selected-entity component
      edits that are currently read-only in the inspector, such as render hint
      toggles, uniform point size/line width, or domain visualization defaults.
      No new helper was required in this slice: mesh primitive-view toggles,
      spatial-debug options, and visualization config already had runtime-owned
      command surfaces, while graph/point-cloud render lanes remain read-only
      status in the domain windows.
- [x] Keep existing core windows available during the transition; menu-opened
      windows are an additive EditorUI workflow for now.

## Tests
- [x] Add `contract;runtime` coverage for pure domain-window model building:
      selected mesh, graph, point-cloud, no selection, stale selection, and
      wrong-domain disabled states.
- [x] Add command-surface tests for any new component-edit helpers, including
      successful selected-entity mutation, stale entity rejection, missing scene
      rejection, and no-change status. No new component-edit helper was added;
      existing primitive-view, spatial-debug, and visualization-config command
      surfaces remain covered by the promoted `SandboxEditorUi` contract tests.
- [x] Extend the existing `SandboxEditorUi` ImGui adapter/callback test so a
      deterministic frame can draw the menu path without requiring Vulkan.
- [x] Ensure tests use existing labels (`contract`, `runtime`, `integration`,
      `headless`) and do not introduce a new CTest label unless both
      `tests/README.md` and `tests/CMakeLists.txt` are updated.

## Docs
- [x] Update `src/app/Sandbox/README.md` to document the `Mesh`, `Graph`, and
      `PointCloud` menu slots and their current EditorUI-only scope.
- [x] Update `src/runtime/README.md` or add a short runtime editor note
      describing the domain-window ownership model and command boundaries.
- [x] Update `tasks/backlog/ui/README.md` and `tasks/backlog/README.md` status
      links when the task is promoted or retired.
- [x] Refresh `docs/api/generated/module_inventory.md` only if public module
      surfaces change.

## Acceptance criteria
- [x] The promoted sandbox EditorUI shows top-level ImGui menu slots for
      `PointCloud`, `Graph`, and `Mesh`.
- [x] Each menu contains submenu items that open domain-specific ImGui windows.
- [x] Domain windows control the selected entity's existing ECS/render
      components through runtime-owned command surfaces and show deterministic
      disabled diagnostics for unsupported selection state.
- [x] `SelectionController` remains the authority for selected/hovered state,
      and UI does not introduce a second selection model.
- [x] `Sandbox::App` remains a runtime-only consumer.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|ImGuiAdapterEngineWiring|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'runtime' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

### Completion session record (2026-06-07)

- `cmake --build --preset ci --target IntrinsicRuntimeContractTests` built
  successfully.
- `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|ImGuiAdapterEngineWiring|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 25/25 tests.
- `ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'runtime' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 376/376 tests.
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  regenerated the public module inventory with 479 modules and no content diff.
- Final combined verification for the retired `RUNTIME-097` + `UI-002` batch:
  `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeTests ExtrinsicSandbox`
  built successfully, `cmake --build --preset ci --target IntrinsicTests`
  built successfully,
  `ctest --test-dir build/ci --output-on-failure -R 'ReferenceScene|SandboxEditorUi|ImGuiAdapterEngineWiring|RuntimeSandboxAcceptance|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 56/56 tests,
  `ctest --test-dir build/ci --output-on-failure -L 'contract' -L 'runtime' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 376/376 tests, and
  `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 2819/2819 tests.
- Final structural checks passed:
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/agents/check_task_state_links.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/repo/check_test_layout.py --root . --strict`, and
  `git diff --check`.

## Forbidden changes
- Importing graphics/RHI ownership into UI or app code.
- Mutating ECS/render components directly from `Sandbox::App`.
- Replacing `SelectionController` with UI-local selection state.
- Hiding missing runtime command surfaces instead of reporting deterministic
  disabled diagnostics.
- Deleting existing core editor panels in the same slice.

## Maturity
- Target: `CPUContracted` for menu/window model building and runtime-owned
  component command surfaces. No `Operational` follow-up is owed for this
  EditorUI-only menu/window slice; a future Vulkan interaction smoke should be
  opened as a separate task only if the implementation adds backend-dependent
  behavior.

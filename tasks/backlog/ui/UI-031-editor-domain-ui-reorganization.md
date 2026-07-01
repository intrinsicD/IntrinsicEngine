---
id: UI-031
theme: F
depends_on: []
maturity_target: CPUContracted
---
# UI-031 — Sandbox EditorUI domain-window reorganization

## Status
- Partial, sliced landing (2026-07-01) on branch
  `claude/ui-backlog-agentic-y3oap2`. Slices B and D and the render-hint /
  binding half of Slice A landed; the Visualization-window merge (rest of
  Slice A) and the per-leaf processing split (Slice C) are deferred.
- Landed: the per-domain `Render` window is renamed to `Appearance`
  (`Mesh/Graph/PointCloud / Appearance`, menu item `Appearance`) and now
  co-locates render hints, bound render-state inspection, property/attribute
  assignment (`DrawPropertyBindingTargets` + `DrawVertexChannelBindingTargets`),
  and texture baking (`DrawTextureBakeControls`) — the last three relocated out
  of the `Properties` window. `Properties` is now a pure property explorer
  (property catalog rows + diagnostics only), with all
  internal/connectivity/generated rows still visible. The menu contract test
  (`DefaultDrawStartsWithOnlyMenuBarVisible`) pins the new `Appearance` titles
  (and the new `ICP Registration` panel from `UI-029`).
- Deferred (own follow-up slices): merging the separate `Visualization` window
  (visualization presets + uniform/lane color) into `Appearance`, and splitting
  the omnibus `Processing` window into per-leaf focused method windows. Both
  require restructuring the `DomainWindowSection` set and the coupled
  `std::array<bool, 15>` domain-window-open storage (a coordinated multi-site
  resize) — deliberately not attempted in this environment, which cannot run the
  C++23-module compiler (clang-18, no vcpkg) to catch a mis-sized array or a
  missed dispatch/menu case. These slices should land where the full
  `cmake --preset ci` + `ctest` gate can verify them.
- Verified in-session: `check_layering --strict`, `check_test_layout --strict`,
  `validate_tasks --strict`, `check_task_policy --strict`, `check_doc_links`,
  call-site consistency greps, and adversarial diff review. Full C++ build/test
  gate deferred to CI.

## Goal
- Reorganize the Mesh, Graph, and PointCloud editor UI so domain properties are pure data exploration, render/visualization/property-assignment controls live in one domain-aware appearance window, texture baking moves out of properties, and processing menu leaves open focused method windows instead of one omnibus processing window.

## Non-goals
- Do not change geometry algorithms, visualization adapters, render extraction, texture-bake command semantics, or method execution behavior.
- Do not add new processing methods beyond the windows already exposed by existing tasks.
- Do not implement frame-pacing diagnostics or performance fixes; `UI-030` owns that investigation.
- Do not redesign the whole editor shell, docking model, or platform window system.

## Context
- Owning subsystem/layer: `Extrinsic.Runtime.SandboxEditorUi` in runtime/editor. The UI may build data-only models and issue runtime-owned commands, but it must not own geometry, renderer, asset, or platform state.
- Current menu shape exposes `Render hints`, `Properties`, `Visualization`, `Selection details`, and `Processing` under each of `PointCloud`, `Graph`, and `Mesh`.
- Current `Properties` windows mix unrelated concerns: bound render-state rows, UV/texture-bake controls, property catalogs, binding targets, vertex-channel binding, and diagnostics.
- Current `Processing` menu leaves such as `Denoise`, `Curvature`, `Remesh`, `Subdivide`, `Vertices > Normals`, and point-cloud outlier removal route to the same domain processing window, which then renders all available controls for that domain.
- The desired user model is:
  - `Properties`: inspect all existing properties and numeric values.
  - `Render` or `Appearance`: render hints, visualization, uniform/lane color, and attribute/property assignment.
  - `Rendering > Texture Baking` or equivalent: UV regeneration and selected-mesh texture bake controls.
  - `Processing > <Method>`: open the respective method window only.

## Required changes
- [ ] Slice A: replace the separate domain `Render hints` and `Visualization` windows with one domain-aware appearance window that renders mesh, graph, or point-cloud controls based on the active domain and selected entity. (Partial — the `Render` window is renamed to `Appearance` and now hosts render hints, bound render-state, and binding/bake; merging the separate `Visualization` window into it is deferred with the section-set restructure.)
- [ ] Slice A: move property-to-render-slot assignment, vertex-channel binding, visualization presets, uniform color, and lane color controls into the appearance window where they belong. (Partial — property-to-render-slot assignment and vertex-channel binding moved into `Appearance`; visualization presets and uniform/lane color stay in the `Visualization` window pending the merge.)
- [x] Slice B: move selected-mesh UV regeneration and texture-bake controls out
      of `Properties` into a mesh rendering/appearance submenu and dedicated
      window. (Relocated into the renamed `Appearance` window rather than a
      separate window.)
- [ ] Slice C: split processing state so each processing menu leaf opens a focused method window (`K-Means`, `Denoise`, `Curvature`, `Remesh`, `Subdivide`, vertex `Normals`, point-cloud `Remove Outliers`, and progressive Poisson where currently exposed) instead of the omnibus domain processing window. (Deferred — needs the `DomainWindowSection`/`bool,15` restructure; see Status.)
- [ ] Slice C: keep an optional processing overview/discovery window only if it adds value as an availability summary; it must not be the primary execution surface. (Deferred with Slice C.)
- [x] Slice D: turn `Mesh / Properties`, `Graph / Properties`, and `PointCloud / Properties` into pure property explorers with fluid scrolling, row selection, and detailed numeric value inspection for selected properties.
- [x] Slice D: preserve visibility of internal/connectivity/generated properties while clearly marking unsupported edit/binding states without hiding rows.
- [x] Update menu contract tests so the intended menu hierarchy is pinned and regressions cannot reintroduce texture baking or method controls under `Properties`. (Window titles pinned: `Appearance` replaces `Render`; `ICP Registration` added.)

## Tests
- [ ] Update `tests/contract/runtime/Test.SandboxEditorUi.cpp` to prove the new domain menu hierarchy and window titles.
- [ ] Add contract coverage proving `Properties` models contain property rows/value-preview data but no texture-bake, UV, render-hint, visualization, or binding-command controls.
- [ ] Add contract coverage proving appearance-window commands still route through existing render-hint, visualization, binding, and vertex-channel command seams.
- [ ] Add contract coverage proving each processing method leaf opens/renders only its focused method controls.
- [ ] Run focused runtime/editor tests for `SandboxEditorUi`, `EditorCommandHistory`, texture-bake command routing, visualization commands, and existing processing commands.

## Docs
- [ ] Update `tasks/backlog/ui/README.md` when slices land or if this task is split.
- [x] Update `src/runtime/README.md` so the `Extrinsic.Runtime.SandboxEditorUi` description matches the new factual menu/window organization. (Added the "Sandbox Editor Appearance / Properties Reorganization" subsection; full-A/Slice-C follow-ups will extend it.)
- [ ] Update any migration/parity docs that still describe `Properties` as the home of texture baking or property binding.

## Acceptance criteria
- [ ] `Mesh / Properties`, `Graph / Properties`, and `PointCloud / Properties` are data-exploration windows only.
- [ ] Texture baking is reachable from a mesh rendering/appearance path and no longer appears in any domain `Properties` window.
- [ ] Render hints, visualization controls, uniform/lane colors, and attribute/property assignment are co-located in one domain-aware appearance window.
- [ ] Processing menu leaves open focused method windows rather than rendering every domain method in one window.
- [ ] Existing runtime command seams and undo/redo behavior are preserved.
- [ ] The refactor is sliced so mechanical/menu organization changes are reviewable separately from the property-value browser upgrade.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|EditorCommandHistory|Visualization|TextureBake|KMeans|VertexNormals|Denoise|Curvature|Remesh|Subdivide|OutlierRemoval' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
```

## Forbidden changes
- Mixing source-file moves with semantic UI behavior changes.
- Moving command ownership out of runtime or introducing live renderer/asset ownership into UI code.
- Hiding properties from the property explorer solely because they are not bindable or bakeable.
- Changing geometry-processing algorithm outputs or texture-bake backend behavior.
- Introducing unrelated feature work.

## Maturity
- Target: `CPUContracted`. The endpoint is a tested runtime/editor organization contract; no `Operational` follow-up is owed unless a later slice adds backend-specific rendering behavior.

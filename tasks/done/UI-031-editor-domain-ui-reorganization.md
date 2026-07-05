---
id: UI-031
theme: F
depends_on: [RUNTIME-138]
maturity_target: CPUContracted
completed: 2026-07-05
---
# UI-031 — Sandbox EditorUI domain-window reorganization

## Status
- Retired on 2026-07-05 at `CPUContracted` on local `main`; PR not opened.
- PR/commit: this retirement commit.
- The final slice consumes the landed visibility-gated selected-entity model
  construction from `RUNTIME-138` without taking ownership of its broader async
  selected-analysis cache/job pipeline.
- The per-domain `Render` window is renamed to `Appearance`
  (`Mesh/Graph/PointCloud / Appearance`, menu item `Appearance`) and now
  co-locates render hints, visualization controls, uniform/lane color controls,
  bound render-state inspection, property/attribute assignment
  (`DrawPropertyBindingTargets` + `DrawVertexChannelBindingTargets`), and
  texture baking (`DrawTextureBakeControls`).
- `Mesh / Properties`, `Graph / Properties`, and `PointCloud / Properties` are
  pure property explorers: they draw property catalog rows plus diagnostics
  only, while internal/connectivity/generated rows remain visible.
- Processing menu leaves now open focused method windows such as
  `Mesh / Processing / Denoise`, `Mesh / Processing / Simplify`,
  `Graph / Processing / Vertices / Normals`, and
  `PointCloud / Processing / Remove Outliers`; the old omnibus per-domain
  `Processing` window is no longer the execution surface.
- Closure verification on 2026-07-05 rebuilt `IntrinsicRuntimeContractTests`
  and reran 330 focused runtime/geometry/graphics tests covering
  `SandboxEditorUi`, command history, visualization, texture baking, K-Means,
  vertex normals, denoise, curvature, remesh, subdivide, simplify, and point-
  cloud outlier removal.

## Goal
- Reorganize the Mesh, Graph, and PointCloud editor UI so domain properties are pure data exploration, render/visualization/property-assignment controls live in one domain-aware appearance window, texture baking moves out of properties, and processing menu leaves open focused method windows instead of one omnibus processing window.

## Non-goals
- Do not change geometry algorithms, visualization adapters, render extraction, texture-bake command semantics, or method execution behavior.
- Do not add new processing methods beyond the windows already exposed by existing tasks.
- Do not implement frame-pacing diagnostics or performance fixes; `UI-030` owns that investigation.
- Do not implement the selected-entity model cache or async derivation pipeline; `RUNTIME-138` owns the nonblocking editor/cache/job architecture and must land first.
- Do not redesign the whole editor shell, docking model, or platform window system.

## Context
- Owning subsystem/layer: `Extrinsic.Runtime.SandboxEditorUi` in runtime/editor. The UI may build data-only models and issue runtime-owned commands, but it must not own geometry, renderer, asset, or platform state.
- Before this task, the domain menu shape exposed `Render hints`,
  `Properties`, `Visualization`, `Selection details`, and `Processing` under
  each of `PointCloud`, `Graph`, and `Mesh`.
- Before this task, `Properties` windows mixed unrelated concerns: bound
  render-state rows, UV/texture-bake controls, property catalogs, binding
  targets, vertex-channel binding, and diagnostics.
- Before this task, processing menu leaves such as `Denoise`, `Curvature`,
  `Remesh`, `Subdivide`, `Vertices > Normals`, and point-cloud outlier removal
  routed to the same domain processing window, which rendered all available
  controls for that domain.
- Domain-window reorganization touches the same `SandboxEditorUi` model
  builders that previously duplicated selected-entity
  property/progressive/texture/visualization work. `RUNTIME-138` still owns the
  broader generation-keyed async selected-analysis cache/job pipeline.
- The desired user model is:
  - `Properties`: inspect all existing properties and numeric values.
  - `Render` or `Appearance`: render hints, visualization, uniform/lane color, and attribute/property assignment.
  - `Rendering > Texture Baking` or equivalent: UV regeneration and selected-mesh texture bake controls.
  - `Processing > <Method>`: open the respective method window only.

## Required changes
- [x] Slice A: replace the separate domain `Render hints` and `Visualization` windows with one domain-aware appearance window that renders mesh, graph, or point-cloud controls based on the active domain and selected entity.
- [x] Slice A: move property-to-render-slot assignment, vertex-channel binding, visualization presets, uniform color, and lane color controls into the appearance window where they belong.
- [x] Slice B: move selected-mesh UV regeneration and texture-bake controls out
      of `Properties` into a mesh rendering/appearance submenu and dedicated
      window. (Relocated into the renamed `Appearance` window rather than a
      separate window.)
- [x] Slice C: split processing state so each processing menu leaf opens a focused method window (`K-Means`, `Denoise`, `Curvature`, `Remesh`, `Subdivide`, vertex `Normals`, point-cloud `Remove Outliers`, and progressive Poisson where currently exposed) instead of the omnibus domain processing window.
- [x] Slice C: keep an optional processing overview/discovery window only if it adds value as an availability summary; it must not be the primary execution surface. The overview is removed as the primary surface; the model still carries capability data for tests and focused controls.
- [x] Slice D: turn `Mesh / Properties`, `Graph / Properties`, and `PointCloud / Properties` into pure property explorers with fluid scrolling, row selection, and detailed numeric value inspection for selected properties.
- [x] Slice D: preserve visibility of internal/connectivity/generated properties while clearly marking unsupported edit/binding states without hiding rows.
- [x] Update menu contract tests so the intended menu hierarchy is pinned and regressions cannot reintroduce texture baking or method controls under `Properties`. (Window titles pinned: `Appearance` replaces `Render`; `ICP Registration` added.)

## Tests
- [x] Update `tests/contract/runtime/Test.SandboxEditorUi.cpp` to prove the new domain menu hierarchy and window titles.
- [x] Add contract coverage proving `Properties` models contain property rows/value-preview data but no texture-bake, UV, render-hint, visualization, or binding-command controls.
- [x] Add contract coverage proving appearance-window commands still route through existing render-hint, visualization, binding, and vertex-channel command seams.
- [x] Add contract coverage proving each processing method leaf opens/renders only its focused method controls.
- [x] Run focused runtime/editor tests for `SandboxEditorUi`, `EditorCommandHistory`, texture-bake command routing, visualization commands, and existing processing commands.

## Docs
- [x] Update `tasks/backlog/ui/README.md` when slices land or if this task is split.
- [x] Update `src/runtime/README.md` so the `Extrinsic.Runtime.SandboxEditorUi` description matches the new factual menu/window organization.
- [x] Update any migration/parity docs that still describe `Properties` as the home of texture baking or property binding. No migration/parity docs required edits in this slice; source search found the factual current-state docs in `src/runtime/README.md` and UI backlog.

## Acceptance criteria
- [x] `Mesh / Properties`, `Graph / Properties`, and `PointCloud / Properties` are data-exploration windows only.
- [x] Texture baking is reachable from a mesh rendering/appearance path and no longer appears in any domain `Properties` window.
- [x] Render hints, visualization controls, uniform/lane colors, and attribute/property assignment are co-located in one domain-aware appearance window.
- [x] Processing menu leaves open focused method windows rather than rendering every domain method in one window.
- [x] Existing runtime command seams and undo/redo behavior are preserved.
- [x] The refactor is sliced so mechanical/menu organization changes are reviewable separately from the property-value browser upgrade.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|EditorCommandHistory|Visualization|TextureBake|KMeans|VertexNormals|Denoise|Curvature|Remesh|Subdivide|OutlierRemoval' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing source-file moves with semantic UI behavior changes.
- Moving command ownership out of runtime or introducing live renderer/asset ownership into UI code.
- Hiding properties from the property explorer solely because they are not bindable or bakeable.
- Changing geometry-processing algorithm outputs or texture-bake backend behavior.
- Introducing unrelated feature work.

## Maturity
- Target: `CPUContracted`. The endpoint is a tested runtime/editor organization contract; no `Operational` follow-up is owed unless a later slice adds backend-specific rendering behavior.

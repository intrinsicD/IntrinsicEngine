# UI Backlog

Editor/UI integration seams that consume runtime/renderer composition and
emit user-driven commands/events. UI must not own simulation state, render
ownership, or asset ownership; it should pass commands/events to owning
systems instead.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [RORG-031F — UI integration backlog seed](RORG-031-ui-integration.md).
- [UI-022 — Sandbox EditorUI vertex-normal recompute windows](UI-022-sandbox-editor-vertex-normal-recompute.md)
  (depends on retired `GEOM-026`; adds the first method window under
  `Mesh > Processing > Vertices`, with graph/point-cloud normal windows
  consuming the geometry-owned recompute modules).

## Convergence

- RORG-031F is part of
  **Theme F — Architecture/runtime/UI foundation seeds**.
- UI-001 is retired as part of **Theme A — Working sandbox app path** and depends on
  `RUNTIME-090` + `GRAPHICS-079` for ImGui frame production/presentation plus
  runtime selection/geometry-residency tasks for live content. RUNTIME-095 closes
  the scoped visual/interactive proof; broad file-backed UI workflows remain
  future work.
- UI-002 is a retired post-acceptance EditorUI follow-up on the promoted
  `SandboxEditorUi` shell: it adds first-class `Mesh`, `Graph`, and
  `PointCloud` ImGui menu slots whose windows control selected-entity
  components through runtime-owned command surfaces.
- UI-003 is a retired promoted EditorUI follow-up that reimplements the legacy
  geometry-processing capability discovery seam against promoted
  `GeometrySources`. The domain menus expose `Processing` windows with
  source-domain, stable algorithm-entry, and K-Means source-domain affordances.
- UI-004 is a retired promoted EditorUI follow-up that reimplements the first
  legacy geometry-processing execution seam: CPU K-Means over mesh vertices,
  graph nodes, and point-cloud points. The command writes legacy-compatible
  label/color properties and marks vertex attributes dirty. `RUNTIME-103`
  records synchronous CPU K-Means as the intended endpoint for current
  workflows; asynchronous scheduling, centroid entities, topology mutation, and
  broader algorithm execution require a future value-gated runtime/method task
  with a concrete workload. `GRAPHICS-086` retires CUDA from the promoted
  default path unless a future method/backend task supplies that workload.
- UI-005 is a retired promoted EditorUI follow-up that reimplements the legacy
  property-enumeration-to-visualization seam for current `GeometrySources`.
  Selected mesh, graph, and point-cloud visualization windows now list eligible
  scalar, isoline, color-buffer, and vector-candidate properties and route
  supported presets through runtime-owned `VisualizationConfig` commands.
  Generic GPU residency/upload for arbitrary property arrays remains future
  runtime/renderer work.
- UI-006 is a retired promoted EditorUI follow-up that reimplements the legacy
  `Frame Graph` diagnostics panel against current `RenderGraphFrameStats`:
  compile/execute counts, queue/timeline stats, command pass statuses,
  diagnostics, and the compiler debug dump are copied through a runtime-owned
  data model and rendered from the attached `SandboxEditorUi`.
- UI-007 is a retired promoted EditorUI follow-up that exposes drag/drop import
  status and payload hints while runtime/platform own events, decoding,
  `AssetService`, and ECS materialization.
- UI-008 is a retired value-gated workflow child from `LEGACY-011`;
  it consumes `RUNTIME-102`
  for command history / dirty-state source of truth. The
  `PLATFORM-006`
  file-dialog boundary is retired: current workflows use runtime/UI path entry
  plus platform dropped-path events, while native dialogs remain deferred unless
  a new platform/runtime task accepts them. Sample/debug scene expansion and
  app-level debug workflow clones remain deferred until a runtime-owned command
  accepts them.
- UI-013 is a retired promoted EditorUI follow-up that turns the UI-002
  render-hint status windows into controls for the existing promoted
  `RenderSurface`, `RenderEdges`, and `RenderPoints` value components.
  Commands are undoable through `EditorCommandHistory`, graph edge-lane edits
  repack runtime graph residency, and uniform point settings flow to retained
  point GPU config. Retained-line per-entity width rasterization remains a
  future renderer task.
- UI-014, UI-016, and UI-017 retired the framework24-style selected-geometry
  usability follow-up after `UI-015`: all geometry properties remain visible,
  compatible binding choices show their reasons, bound-state rows report render
  lanes, slots, defaults, properties, textures, readiness, diagnostics, and
  derived-job/bake progress, and mesh UV/bake controls route through runtime
  command surfaces without UI owning geometry, runtime, asset, or graphics
  state.
- UI-018 retired the sandbox startup organization follow-up: the editor now
  starts menu-first, with top-level panels available from `View` and all
  sandbox/domain windows closed until explicitly opened.
- UI-019 retired the visualization uniform-color edit follow-up: mesh, graph,
  point-cloud, and top-level geometry visualization windows show an ImGui color
  edit widget when `VisualizationConfig::ColorSource::UniformColor` is active,
  with edits routed through the existing runtime visualization command seam.
- UI-020 retired the visualization lane-color follow-up: mesh, graph, and
  point-cloud domain visualization windows now target surface, edge, and point
  render lanes by source-row presence, with optional lane overrides letting mesh
  vertices and graph nodes use point-lane uniform colors independently of edge
  and surface visualization.
- UI-021 retired the migration that removed the remaining UI-local
  availability policy from `Runtime.SandboxEditorUi`: domain windows,
  visualization controls, property catalogs, and processing affordances now
  consume the runtime resolver from `RUNTIME-117`, while preserving provenance
  labels for mesh/graph/point-cloud origins.
- UI-022 is the opened follow-up from the vertex-processing menu request: the
  mesh `Processing > Vertices` submenu gains a `Normals` method window backed
  by `Geometry.HalfedgeMesh.Vertices.Normals`; graph and point-cloud windows
  now consume retired `GEOM-026` recomputation contracts for those domains.
- UI-023 retired the render recipe editing follow-up: sandbox UI now inspects
  renderer descriptors, optional recipe slots, binding overrides, view/output
  recipes, validation/preview status, artifact lifetime/status, and activation
  outcomes without UI owning renderer state.
- UI work that depends on renderer overlays/handoff coordinates with the
  retired `GRAPHICS-024`
  parity matrix, the `RUNTIME-104` decision not to retain a persistent
  derived-overlay producer for current workflows, and the rendering DAG in
  [`tasks/backlog/rendering/README.md`](../rendering/README.md).

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [UI-001 — Sandbox editor shell and core panels](../../done/UI-001-sandbox-editor-shell-panels.md) (done, 2026-06-03, `CPUContracted`).
- [UI-002 — Sandbox EditorUI domain menu windows](../../done/UI-002-editor-domain-menu-windows.md) (done, 2026-06-07, `CPUContracted`).
- [UI-003 — Sandbox EditorUI geometry processing capabilities](../../done/UI-003-sandbox-editor-geometry-processing-capabilities.md) (done, 2026-06-07, `CPUContracted`).
- [UI-004 — Sandbox EditorUI K-Means execution command seam](../../done/UI-004-sandbox-editor-kmeans-execution.md) (done, 2026-06-07, `CPUContracted`).
- [UI-005 — Sandbox EditorUI visualization property presets](../../done/UI-005-sandbox-editor-visualization-property-presets.md) (done, 2026-06-07, `CPUContracted`).
- [UI-006 — Sandbox EditorUI render graph diagnostics panel](../../done/UI-006-sandbox-editor-rendergraph-panel.md) (done, 2026-06-08, `CPUContracted`).
- [UI-007 — Sandbox EditorUI drag/drop import status](../../done/UI-007-sandbox-editor-drag-drop-import.md) (done, 2026-06-08, `CPUContracted`).
- [UI-008 — Editor file dialog, dirty-state, and debug workflows](../../done/UI-008-editor-file-dialog-dirty-debug-workflows.md) (done, 2026-06-09, `CPUContracted`):
  path-entry file boundary, dirty-state/undo-redo affordances, new/open/save/close
  command routing, and app-to-runtime-only dependency proof.
- [UI-013 — Sandbox EditorUI domain render hint controls](../../done/UI-013-domain-render-hint-controls.md) (done, 2026-06-11, `CPUContracted`).
- [UI-015 — Progressive render-data inspector](../../done/UI-015-progressive-render-data-inspector.md)
  (done, 2026-06-16, `CPUContracted`): selected-entity progressive inspector
  models and ImGui rows now expose entity shape, presentation slots, uniform
  defaults, source-property pickers, compatible/incompatible reasons, derived
  jobs, and composition summaries through runtime-owned command/history seams.
- [UI-016 — Geometry property catalog and binding usability](../../done/UI-016-geometry-property-catalog-and-binding-usability.md)
  (done, 2026-06-17, `CPUContracted`): selected mesh, graph, and point-cloud
  property catalogs now list internal/connectivity/user/generated properties,
  supported value previews, unsupported diagnostics, and compatible binding
  targets without raw property pointers.
- [UI-017 — Bound render state inspector](../../done/UI-017-bound-render-state-inspector.md)
  (done, 2026-06-17, `CPUContracted`): selected mesh, graph, point-cloud, and
  composition models now expose render lanes, progressive slots, source kind,
  bound defaults/properties/textures, readiness diagnostics, and derived-job
  progress rows.
- [UI-014 — UV backend and texture bake controls](../../done/UI-014-uv-backend-and-texture-bake-controls.md)
  (done, 2026-06-17, `CPUContracted`): selected-mesh UV diagnostics,
  xatlas-backed regeneration commands, property-catalog-driven bake sources,
  target semantic/encoder/output-size controls, and selected-mesh bake command
  routing are now present in the runtime-owned sandbox editor UI.
- [UI-018 — Sandbox menu-first UI defaults](../../done/UI-018-sandbox-menu-first-ui.md)
  (done, 2026-06-17, `CPUContracted`): sandbox startup now shows only the
  main menu bar; top-level panels open from `View`, while domain windows remain
  closed until opened from the PointCloud/Graph/Mesh menus.
- [UI-019 — Visualization uniform color edit widget](../../done/UI-019-visualization-uniform-color-edit.md)
  (done, 2026-06-19, `CPUContracted`): selected mesh, graph, point-cloud, and
  top-level geometry visualization windows expose an ImGui color edit widget
  for active uniform-color visualization configs without UI owning renderer or
  asset state.
- [UI-020 — Visualization lane uniform color controls](../../done/UI-020-visualization-lane-uniform-color.md)
  (done, 2026-06-19, `CPUContracted`): domain visualization windows target
  surface, edge, and point render lanes by source-row presence, and optional
  lane overrides let mesh vertices and graph nodes use independent uniform
  point-lane colors.
- [UI-021 — Sandbox editor geometry availability migration](../../done/UI-021-sandbox-editor-geometry-availability-migration.md)
  (done, 2026-06-19, `CPUContracted`): domain windows, visualization targets,
  property catalogs, primitive-view commands, render hints, K-Means affordances,
  and mesh UV/bake diagnostics now consume the runtime availability resolver.
- [UI-023 — Sandbox render recipe editing UI](../../done/UI-023-render-recipe-ui-editing.md)
  (done, 2026-06-24, `CPUContracted`): render recipe editor models and ImGui
  rows expose declared renderer slots, binding overrides, view/output recipes,
  validation/preview/activation state, and artifact publish/apply commands
  through runtime-owned seams without UI owning renderer state.

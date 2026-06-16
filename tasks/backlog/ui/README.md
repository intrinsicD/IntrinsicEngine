# UI Backlog

Editor/UI integration seams that consume runtime/renderer composition and
emit user-driven commands/events. UI must not own simulation state, render
ownership, or asset ownership; it should pass commands/events to owning
systems instead.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [RORG-031F — UI integration backlog seed](RORG-031-ui-integration.md).
- [UI-014 — UV backend and texture bake controls](UI-014-uv-backend-and-texture-bake-controls.md):
  expose resolved-UV provenance, backend selection/regeneration, and generic
  mesh attribute texture bake commands through runtime-owned UI command seams.

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
- UI-008 is a retired value-gated workflow child from
  [`LEGACY-011`](../architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md);
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

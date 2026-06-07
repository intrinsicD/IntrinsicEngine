# UI Backlog

Editor/UI integration seams that consume runtime/renderer composition and
emit user-driven commands/events. UI must not own simulation state, render
ownership, or asset ownership; it should pass commands/events to owning
systems instead.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [RORG-031F — UI integration backlog seed](RORG-031-ui-integration.md).
- [UI-001 — Sandbox editor shell and core panels](../../done/UI-001-sandbox-editor-shell-panels.md) (done, 2026-06-03, `CPUContracted`).
- [UI-002 — Sandbox EditorUI domain menu windows](../../done/UI-002-editor-domain-menu-windows.md) (done, 2026-06-07, `CPUContracted`).
- [UI-003 — Sandbox EditorUI geometry processing capabilities](../../done/UI-003-sandbox-editor-geometry-processing-capabilities.md) (done, 2026-06-07, `CPUContracted`).
- [UI-004 — Sandbox EditorUI K-Means execution command seam](../../done/UI-004-sandbox-editor-kmeans-execution.md) (done, 2026-06-07, `CPUContracted`).
- [UI-005 — Sandbox EditorUI visualization property presets](../../done/UI-005-sandbox-editor-visualization-property-presets.md) (done, 2026-06-07, `CPUContracted`).

## Convergence

- RORG-031F is part of **Theme F — Architecture/runtime/UI foundation seeds**.
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
  label/color properties and marks vertex attributes dirty; CUDA/asynchronous
  scheduling, centroid entities, topology mutation, and broader algorithm
  execution remain future runtime/editor work.
- UI-005 is a retired promoted EditorUI follow-up that reimplements the legacy
  property-enumeration-to-visualization seam for current `GeometrySources`.
  Selected mesh, graph, and point-cloud visualization windows now list eligible
  scalar, isoline, color-buffer, and vector-candidate properties and route
  supported presets through runtime-owned `VisualizationConfig` commands.
  Generic GPU residency/upload for arbitrary property arrays remains future
  runtime/renderer work.
- UI work that depends on renderer overlays/handoff coordinates with the
  retired [`GRAPHICS-024`](../../done/GRAPHICS-024-overlays-presentation-editor-handoff.md)
  parity matrix and the rendering DAG in
  [`tasks/backlog/rendering/README.md`](../rendering/README.md).

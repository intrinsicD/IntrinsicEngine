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
- UI work that depends on renderer overlays/handoff coordinates with the
  retired [`GRAPHICS-024`](../../done/GRAPHICS-024-overlays-presentation-editor-handoff.md)
  parity matrix and the rendering DAG in
  [`tasks/backlog/rendering/README.md`](../rendering/README.md).

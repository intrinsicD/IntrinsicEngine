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

## Convergence

- RORG-031F is part of **Theme F — Architecture/runtime/UI foundation seeds**.
- UI-001 is retired as part of **Theme A — Working sandbox app path** and depends on
  `RUNTIME-090` + `GRAPHICS-079` for ImGui frame production/presentation plus
  runtime selection/geometry-residency tasks for live content. Final
  file-backed visual/interactive proof remains owned by `RUNTIME-095`.
- UI work that depends on renderer overlays/handoff coordinates with the
  retired [`GRAPHICS-024`](../../done/GRAPHICS-024-overlays-presentation-editor-handoff.md)
  parity matrix and the rendering DAG in
  [`tasks/backlog/rendering/README.md`](../rendering/README.md).

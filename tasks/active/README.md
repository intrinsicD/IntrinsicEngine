# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [RUNTIME-174 — Privatize the ImGui editor bridge surface](RUNTIME-174-privatize-imgui-editor-bridge-surface.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  Engine-only bridge is moving from a standalone exported module to private
  Engine module glue without changing overlay, callback, capture, or lifecycle
  behavior; next gate is the focused runtime contract/integration build.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

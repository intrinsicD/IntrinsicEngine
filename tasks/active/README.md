# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-085 — ImGui overlay drops draw-command clip rectangles](BUG-085-imgui-overlay-drops-command-clip-rectangles.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The live
  UI-036 Vulkan run exposed missing `ImDrawCmd::ClipRect` propagation and
  per-command scissor recording; next gate is the focused runtime/graphics
  ImGui contract pair.
- [UI-036 — Sandbox parameterization editor panel and resizable UV split view](UI-036-sandbox-parameterization-editor-and-uv-split-view.md)
  (`blocked` by `BUG-085`; owner: Codex; branch:
  `codex/arch-006-completion`). Its CPU/full/live functionality gates passed;
  retirement resumes after the overlay clip/scissor defect is repaired and
  the production Vulkan interaction is replayed.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

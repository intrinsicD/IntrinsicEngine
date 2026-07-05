# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [UI-030 — Sandbox EditorUI frame-pacing diagnostics](UI-030-editor-frame-pacing-diagnostics.md)
  is in progress at `CPUContracted`: runtime, ImGui producer, and Vulkan
  lifecycle timing diagnostics are instrumented and covered by focused
  CPU/null contract tests plus a selected opt-in `gpu;vulkan` smoke on this
  host. Next verification: add the bounded diagnostic harness/report required
  for `Operational` retirement.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

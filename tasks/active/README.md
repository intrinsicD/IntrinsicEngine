# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-082 — GLFW X11 input-method initialization leaks under LeakSanitizer](BUG-082-glfw-x11-input-method-lsan-leak.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  source-only regression patch is audited against current head; next gate is
  live X11 sanitizer execution plus the unsuppressed synthetic-leak control.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

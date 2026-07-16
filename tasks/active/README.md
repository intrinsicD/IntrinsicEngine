# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-083 — Vulkan Sandbox shutdown reports driver and DBus leaks under LeakSanitizer](BUG-083-vulkan-sandbox-shutdown-lsan-leaks.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). Fresh
  exact-head NVIDIA evidence reproduces the three diagnosed external-retention
  paths; next gate is the narrow-policy process contract and synthetic engine
  leak control.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

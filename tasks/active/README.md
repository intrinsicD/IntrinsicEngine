# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-092 — Scene lifecycle async wait exhausts its frame budget under delayed I/O](BUG-092-scene-lifecycle-async-wait-frame-budget.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). Root cause
  is confirmed in the runtime contract harness; next gate is the bounded
  steady-clock/yield repair and repeated queued scene-file coverage.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

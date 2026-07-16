# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-098 — Frame clock samples an incomplete frame delta](BUG-098-frame-clock-samples-incomplete-frame-delta.md)
  (`in-progress`; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is the
  completed-frame clock regression plus production-delay real-control hover.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

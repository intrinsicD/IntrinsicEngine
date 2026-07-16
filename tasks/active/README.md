# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-081` — Warm-configure CI budget still flakes on hosted-runner variance](BUG-081-warm-configure-budget-runner-variance.md)
  is collecting same-context hosted exact-cache-hit populations after PR #1024
  reproduced the failure at `30.368 s`; next apply the documented
  population-plus-headroom rule and re-run every PR gate.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

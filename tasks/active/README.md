# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-104` — Kernel-convergence regression asserts a retired snapshot](BUG-104-kernel-convergence-regression-stale-snapshot.md)
  is synchronizing the deterministic `pr-fast` repository-snapshot assertion
  with the already-ratcheted live policy; next pass all 19 regressions and the
  strict checker, then require repaired-head `pr-fast` to pass.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

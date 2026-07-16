# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-088` — Benchmark smoke hard timeout flakes under host contention](BUG-088-benchmark-smoke-hard-timeout-host-contention.md):
  classify the 22-result smoke from a seven-run hosted timing population while
  preserving its dedicated fail-closed runner and strict result validation.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

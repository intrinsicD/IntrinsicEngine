# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`CI-003`](CI-003-ci-gate-timing-observability-and-cancellation.md) —
  CI gate timing observability and stale-run cancellation. Slice A defines the
  stable benchmark/result contract and aggregation regressions; Slice B wires
  compile-heavy workflows and concurrency; Slice C backfills the claim-grade
  historical baseline.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

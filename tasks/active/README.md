# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-112`](BUG-112-clang-source-coverage-production-path-instability.md) —
  remove two diagnosed production-path instabilities that prevent strict
  Clang 20 source-coverage parity from completing.
- [`CI-011`](CI-011-measured-slow-test-cohort.md) — collect comparable hosted
  per-case timing evidence, split only measured heavy variants, and retain
  complete fast-sentinel plus scheduled-slow coverage.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

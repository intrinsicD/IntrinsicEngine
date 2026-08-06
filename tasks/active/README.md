# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`REVIEW-003`](REVIEW-003-architecture-stability-right-sizing-readiness-audit.md) —
  in progress on `main` by `codex-review003`; rerun the complete architecture,
  clean-workshop, drift, agent-output, and right-sizing audit from a fresh
  clean commit, then obtain independent fixed-surface acceptance.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

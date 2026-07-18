# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`CORE-008`](CORE-008-compiled-taskgraph-plan-reuse.md) — exact
  registration-replay plan reuse with fixed-step and renderer-prep adoption;
  owner: Codex; branch: `codex/core-008-compiled-plan-reuse`; next gate:
  freeze the fallback replay benchmark harness and capture its `ci-release`
  baseline before implementing reuse.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`CORE-007`](CORE-007-scheduler-priority-wait-wake-hardening.md) —
  scheduler priority lanes and measured conditional-wake hardening; owner:
  Codex; branch: `codex/core-007-scheduler-hardening-v2`; next gate: baseline
  the `ci-release` scheduler smoke benchmark before production changes.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

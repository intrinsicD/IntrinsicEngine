# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- `UI-039` — Semantic point-set slot binding and LOP discovery
  (`in-progress`; owner: `codex-slot-binding`; branch: `main`). Implementation
  and CPU verification are complete; next action is the required independent
  high-risk review against the final evidence revision/content digest.
- `PHYSICS-004` — Operational runtime physics module and bridge privatization
  (`in-progress`; owner: `codex-root`; branch:
  `feature/physics-004-runtime-module`). Technical acceptance and driver
  verification are complete; next action is an independent high-risk review
  against the final evidence revision/content digest.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`RUNTIME-215` — Organize runtime sources by cohesive ownership](RUNTIME-215-organize-runtime-source-layout.md)
  has completed implementation and runtime-scoped verification under
  `codex-root` on `main`. Its next step is standard evidence sealing after the
  unrelated repository-global `BUG-133` task-state finding and active
  `RUNTIME-214` review-evidence finding are cleared.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

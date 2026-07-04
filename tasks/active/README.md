# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-117`](GRAPHICS-117-render-graph-compile-cache.md) — in
  progress on `main` by Codex. Slice A verified renderer compile-cache reuse
  and lazy debug dump gating. Next slice: PR-fast declare/compile benchmark
  evidence and retirement if the cache contract remains stable.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [RUNTIME-170 — Privatize the object-space normal GPU queue surface](RUNTIME-170-privatize-object-space-normal-gpu-queue-surface.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  single-consumer queue module is being folded into service-owned private
  state without changing bake behavior; next gate is the focused
  queue/service contract build and test.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

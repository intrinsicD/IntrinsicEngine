# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [METHOD-023 — Boundary First Flattening (BFF) reference backend](METHOD-023-boundary-first-flattening-reference-backend.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The CPU
  reference, correctness tests, benchmark, docs, and full default CPU gate are
  complete; the task is ready to retire at `CPUContracted`.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [RUNTIME-178 — Restore the pre-ratchet Runtime.Engine convergence budget](RUNTIME-178-restore-engine-convergence-budget.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  right-sized slice makes two Engine-private services opaque and routes UV
  queries through the existing registered extraction cache; next gate is the
  focused runtime build and exact convergence ratchet.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

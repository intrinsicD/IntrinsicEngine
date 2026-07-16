# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-090 — Async-work layering test asserts stale shutdown call spelling](BUG-090-async-work-layering-test-stale-shutdown-owner.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  opt-in runtime layering binary has one stale Engine shutdown spelling; next
  gate is the exact pre-fix reproduction and two-assertion correction.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

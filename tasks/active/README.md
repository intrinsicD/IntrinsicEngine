# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`PROC-032 — Repository-native agent work graph`](PROC-032-repository-native-agent-work-graph.md)
  — driver implementation, verification, and handoff complete; owner
  `Codex-AgentGraph`; branch `main`; the default graph exercised a blocking
  review/reopen cycle and is now waiting at the required independent high-risk
  review node. It must not finalize or retire before that acceptance.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

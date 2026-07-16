# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [RUNTIME-173 — Privatize the K-Means GPU job queue surface](RUNTIME-173-privatize-kmeans-gpu-job-queue-surface.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  Sandbox-session-only queue class is moving behind the existing Sandbox editor
  facade while its request/result DTO contract remains public; next gate is the
  focused Sandbox, K-Means, and JobService build.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

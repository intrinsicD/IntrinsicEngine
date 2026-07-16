# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [HARDEN-085 — Enforce the Runtime.Engine kernel-convergence ratchet](HARDEN-085-enforce-runtime-engine-kernel-convergence-ratchet.md)
  (`in-progress`; owner: Codex; branch: `codex/arch-006-completion`). The
  bounded tooling slice records current convergence debt explicitly and wires
  a no-backsliding guard plus synthetic failures into `pr-fast`.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

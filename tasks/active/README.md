# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`ARCH-014`](ARCH-014-kernel-convergence-tracking.md) — kernel convergence
  umbrella; owner: Codex; coordination branch:
  `codex/arch-014-kernel-convergence-program`; blocked on final leaf
  `RUNTIME-187`. Next gate: execute the unblocked ADR-0027 behavior-owner
  leaves and update the convergence scorecard after each retirement.
- [`RUNTIME-168`](RUNTIME-168-privatize-sandbox-default-policies-surface.md) —
  Sandbox default-policy surface privatization; owner: Codex team; branch:
  `codex/runtime-168-policy-surface`; in progress. Next gate: replace the
  one-consumer lifecycle helper with four existing-facade descriptor factories
  and app-private transactional registration handles.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

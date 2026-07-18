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
  `codex/arch-014-kernel-convergence-program`; blocked on `ARCH-016` and
  final leaf `RUNTIME-187`. Next gate: retire the right-sizing amendment, then
  execute the seeded behavior-owner graph.
- [`ARCH-016`](ARCH-016-right-size-runtime-composition-target.md) — runtime
  composition-mechanism right-sizing amendment; owner: Codex; branch:
  `codex/arch-016-runtime-composition-target`; ADR-0027 and the evidence-backed
  child graph are written. Next gate: strict validation, review, and retirement.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

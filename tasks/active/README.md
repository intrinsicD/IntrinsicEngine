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
  `RUNTIME-129`. Next gate: retire the right-sizing amendment before seeding
  implementation children.
- [`ARCH-016`](ARCH-016-right-size-runtime-composition-target.md) — runtime
  composition-mechanism right-sizing amendment; owner: Codex; branch:
  `codex/arch-016-runtime-composition-target`; next gate: write ADR-0027 and
  seed the evidence-backed ARCH-014 child graph.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

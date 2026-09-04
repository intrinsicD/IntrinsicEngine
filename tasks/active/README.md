# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-166` — clean scratch experiments lose historical input
  seals](BUG-166-clean-scratch-experiment-historical-input-seals.md) — in
  progress; preserve exact clean-revision source-input validation for
  non-claim scratch custody, beginning with the hermetic experiment-custody
  regression.
- [`BUG-164` — ccache serves stale objects when a macro changes only imported
  module BMIs](BUG-164-ccache-module-bmi-macro-staleness.md) — in progress;
  dependency-local semantic sidecars pass local fixture, core, full-graph, and
  exact graphics evidence; hosted warm-budget evidence remains open.

## Records

Retirement records live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

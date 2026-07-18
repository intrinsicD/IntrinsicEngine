# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-114`](BUG-114-ci-release-architecture-slo-calibration.md) — repair the
  mismatched workloads, metrics, and uncalibrated guardrails exposed by the
  first hosted Release architecture-SLO pilot.
- [`CI-009`](CI-009-heavy-gate-routing-and-runner-evaluation.md) — blocked by
  `BUG-114` after failed Release pilot `29631970411`; resume with a fresh
  five-sample unchanged-SHA population only after the blocker retires.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

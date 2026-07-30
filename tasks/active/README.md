# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-120` — Test.WorkflowConcurrency drifted from the CPU test sources it mirrors](BUG-120-workflow-concurrency-ctest-processors-drift.md)
  is reproducing four deterministic snapshot/parity failures on current
  `main`; the task is reconciling the test and case-scoped CTest reservations
  with the authoritative CPU test sources.
- [`PROC-028` — Enforce agent evidence, review, and experiment custody](PROC-028-enforced-agent-evidence-review-experiment-workflow.md)
  is implemented and integrated on local `main`. Retirement remains blocked
  on a distinct fixed-revision review and pre-existing `BUG-120`.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

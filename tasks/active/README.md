# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-105` — Runtime module reader races ECS structural mutation](BUG-105-runtime-module-ecs-structural-hazard.md):
  diagnose and close the ASan-proven EnTT storage-map race with the existing
  FrameGraph structural hazard token; local clean sanitizer and full CPU gates
  pass, and the remaining closure gate is repaired exact-head `pr-fast`.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

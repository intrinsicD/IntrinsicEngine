# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-130`](GRAPHICS-130-retire-unused-rhi-pipeline-registry.md) —
  retire the zero-production-consumer RHI PipelineRegistry while preserving
  PipelineManager and renderer-owned pipeline behavior. `REVIEW-003` remains
  in the architecture backlog until this and its other audit remediations
  retire.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-060`](BUG-060-scalar-colormap-lut-1d-view-black-on-gpu.md) —
  scalar/isoline colormap LUT GPU black-output fix. Status: implemented;
  verification owed before retirement. Next verification: run the default
  CPU gate plus the manual Vulkan sandbox Scalar/Isolines preset check.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

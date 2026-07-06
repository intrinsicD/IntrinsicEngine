# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-119`](GRAPHICS-119-parallel-pass-command-recording.md) —
  parallel render-pass command recording via the task scheduler. Slices A and B
  are complete locally: the executor has a backend-neutral layer-parallel
  record/join contract, RHI has a per-pass command-context acquisition seam,
  Null/Mock provide CPU bookkeeping, and the renderer exposes a debug selector
  with serial fallback stats. Slice C.1 adds Vulkan backend-local secondary
  command buffers for accepted graphics-queue parallel context plans. Slice C.2
  exposes the renderer worker fan-out stats, and Slice C.3 isolates
  command-record diagnostics behind a guarded frame-local accumulator. Scheduler
  dispatch stays disabled because dynamic upload helpers, readback counters,
  shared pass helper state, and Vulkan command-pool ownership still need
  isolation or synchronization. Benchmark evidence and the `gpu;vulkan` smoke
  remain later slices.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

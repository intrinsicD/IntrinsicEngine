# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-119`](GRAPHICS-119-parallel-pass-command-recording.md) —
  parallel render-pass command recording via the task scheduler. Slice A is
  complete locally: `RenderGraphExecutor` now has a backend-neutral
  layer-parallel record/join contract with CPU/null determinism, worker
  distribution, and fail-closed tests. Next work is Slice B: RHI/null
  command-context acquisition and renderer serial-fallback/debug selection.
  Vulkan secondary command buffers, benchmark evidence, and the `gpu;vulkan`
  smoke remain later slices.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-120`](GRAPHICS-120-framegraph-compiler-executor-efficiency.md) —
  framegraph compiler/executor efficiency and hygiene polish. Slice A is
  complete locally: `TextureUsage::ColorAttachmentRead` uses a read-only
  barrier state with a focused CPU render-graph regression. Broader
  efficiency/hygiene work remains in the active task file.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

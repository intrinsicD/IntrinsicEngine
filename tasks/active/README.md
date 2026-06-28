# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-104` — GPU Object-Space Normal Texture Bake](GRAPHICS-104-gpu-object-space-normal-texture-bake.md)
  — in progress; next verification step is focused object-space normal bake
  contract coverage plus the default CPU-supported gate after runtime
  orchestration lands.
- [`GRAPHICS-107` — Reconcile the FrameRecipe vs RenderRecipe vocabularies](GRAPHICS-107-reconcile-framerecipe-renderrecipe-vocabulary.md)
  — in progress; Slice A docs/locality landed, next verification step is the
  Slice B projection-function contract test plus the default CPU-supported gate.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

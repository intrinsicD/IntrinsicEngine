# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md`](GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md)
  — in-progress (Slice A). Theme A leaf; depends on GRAPHICS-070 (done).
  Slice A lands the EntityId selection pipeline + `"PickingPass"` executor
  route + GpuScene-aware `selection/entity_id.{vert,frag}` shader pair.
  Branch: `claude/setup-agentic-workflow-mf8d0`.

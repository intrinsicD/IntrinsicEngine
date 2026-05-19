# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md`](GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md)
  — in-progress (recipe-side follow-up between Slice A and Slice B).
  Theme A leaf; depends on GRAPHICS-070 (done). Slice A merged via PR #890
  (commits `ad2e40d` + `08af46e`, merge `558a75d`). The follow-up reorders
  `PickingPass` after `DepthPrepass`, adds `Read(SceneDepth, DepthRead)`,
  gates picking on `EnablePicking && EnableDepthPrepass`, and flips the
  EntityId selection pipeline back to the canonical depth-equal /
  `D32_FLOAT` shape so the readback drain (Slice D) returns
  nearest-surface IDs instead of last-fragment-winning.
  Branch: `claude/setup-agentic-workflow-Xrb7P`.

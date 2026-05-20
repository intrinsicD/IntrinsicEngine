# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md`](GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md)
  — in-progress (Slice D.1 next; Slice D split into D.1/D.2/D.3/D.4
  sub-slices). Theme A leaf; depends on GRAPHICS-070 (done). Slice A
  merged via PR #890 (commits `ad2e40d` + `08af46e`, merge `558a75d`).
  The recipe-side follow-up between Slice A and Slice B (reorder
  `PickingPass` after `DepthPrepass`, add `Read(SceneDepth, DepthRead)`,
  gate picking + its `EntityId` / `PrimitiveId` / `Picking.Readback`
  resources on `EnablePicking && EnableDepthPrepass`, flip the EntityId
  selection pipeline to depth-equal / `D32_FLOAT`) merged via PR #891
  (commits `dac6f47` + `b78347d`, merge `5b5309d`). Slice B
  (Face/Edge/Point selection ID pipelines + executor branch fan-out +
  GpuScene-aware face/edge/point shader pairs) landed on
  `claude/setup-agentic-workflow-2Jgf2`. Slice C (selection outline
  pipeline + `"SelectionOutlinePass"` executor route) landed on
  `claude/setup-agentic-workflow-htZjF`. The Slice D split into
  D.1..D.4 sub-slices landed on
  `claude/setup-agentic-workflow-duMR0` (task-file only — no source
  changes). Slice D.1 (renderer-owned host-visible `Picking.Readback`
  buffer lifecycle, no copy / no drain) opens on a new agent branch;
  D.2 wires the recipe import + per-frame copy; D.3 wires the
  `BeginFrame` drain + `PublishPickResult` / `PublishNoHit`; D.4
  plumbs the outline push constants.

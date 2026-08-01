# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`RUNTIME-175` — Point-cloud consolidation runtime operation and config lane](RUNTIME-175-pointcloud-consolidation-runtime-config-integration.md)
  (`Codex-GeometryE2E`, branch `feature/lop-consolidation-e2e`): promote the
  accepted LOP/WLOP/CLOP/EAR CPU references through one runtime module,
  config lane, existing ECS geometry sources, and undoable writeback.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

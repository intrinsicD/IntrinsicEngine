# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-158 — Geometry readiness during enrichment](BUG-158-direct-mesh-enrichment-blocks-usable-geometry.md)
  — in progress on `codex/bug-158-ready-during-enrichment`; CPU regression and
  pending-enrichment Vulkan readback are the next verification steps.

## Records

Retirement records live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

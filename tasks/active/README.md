# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-098 — Frame clock samples an incomplete frame delta](BUG-098-frame-clock-samples-incomplete-frame-delta.md)
  (`in-progress`; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is the
  completed-frame clock regression plus production-delay real-control hover.
- [BUG-099 — Binary PLY point-cloud import rejects face-list elements](BUG-099-binary-ply-pointcloud-skips-face-lists.md)
  (`in-progress`; owner: Codex geometry worker; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is endian-safe
  non-vertex list consumption plus malformed binary PLY regressions.
- [BUG-101 — Fast-staged UV edge grouping is quadratic](BUG-101-fast-staged-uv-edge-grouping-quadratic.md)
  (`in-progress`; owner: Codex geometry worker; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is deterministic
  near-linear grouping plus declared, baseline-compared scaling evidence.
- [BUG-100 — Manual geometry import blocks the Sandbox frame loop](BUG-100-manual-geometry-import-blocks-frame-loop.md)
  (`in-progress`; owner: Codex runtime worker; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is a real
  Null-window queued import that advances frames across a blocked decode.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

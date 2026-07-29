# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`RUNTIME-201` — Unified editor mutation and history transaction](RUNTIME-201-unified-editor-mutation-history-transaction.md)
  is in progress on `codex/runtime-201-unified-editor-mutation`; Slice A's
  internal transaction helper and atomicity/staleness contracts are complete,
  direct/ICP transform publication and coalesced gizmo drag commit now use it,
  `GizmoUndoStack` is deleted, migrated mesh property/topology owners include
  UV regeneration, point-cloud replacement now validates the full point source,
  parameterization validates its exact semantic solver source, and the two
  render-hint component edit routes are the next owner-scoped adoption.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

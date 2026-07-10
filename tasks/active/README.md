# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`CI-004`](CI-004-label-derived-test-build-aggregates.md) — derive
  gate-specific build aggregates from the same target/label metadata that
  drives CTest selection, then route PR-fast and Vulkan away from the complete
  test aggregate.
- [`CI-007`](CI-007-module-safe-persistent-ccache-pilot.md) — pilot a bounded
  external ccache store in `pr-fast`, with module-safe hashing, fail-closed
  telemetry, and cached-versus-clean interface-change parity.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

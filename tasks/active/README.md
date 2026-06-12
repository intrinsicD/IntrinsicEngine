# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-032` — Triangle edge and point rendering is invisible on Vulkan](BUG-032-triangle-edge-point-vulkan-rendering.md):
  active rendering diagnosis; verifies the promoted triangle edge/point lanes beyond the CPU/null contract.
- [`INFRA-001` — Move third-party dependencies to a vcpkg manifest](INFRA-001-vcpkg-manifest-mode.md):
  active dependency-management migration slice; unrelated to current runtime/UI work.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

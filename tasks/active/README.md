# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`METHOD-013` — Progressive Poisson-disk sampling: GPU (Vulkan compute) backend + parity](METHOD-013-progressive-poisson-disk-gpu-backend.md)
  — in progress; Slice B pins the Vulkan shader/layout/dispatch-planning
  contract and CPU fallback status. Next implementation step is Slice C
  conflict-checked dispatch recording over GRAPHICS-108 compaction.
- [`RUNTIME-125` — Optional AoS fast lane for static geometry](RUNTIME-125-aos-static-fast-lane.md)
  — in progress; next verification step is Slice C focused graphics contract
  coverage plus opt-in `gpu;vulkan` parity smoke for the AoS storage/shader path.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

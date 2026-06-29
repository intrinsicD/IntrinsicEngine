# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-104` — GPU Object-Space Normal Texture Bake](GRAPHICS-104-gpu-object-space-normal-texture-bake.md)
  — in progress; next verification step is focused object-space normal bake
  contract coverage plus the default CPU-supported gate after runtime
  orchestration lands.
- [`RUNTIME-125` — Optional AoS fast lane for static geometry](RUNTIME-125-aos-static-fast-lane.md)
  — in progress; next verification step is Slice C focused graphics contract
  coverage plus opt-in `gpu;vulkan` parity smoke for the AoS storage/shader path.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

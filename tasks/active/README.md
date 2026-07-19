# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`ARCH-014`](ARCH-014-kernel-convergence-tracking.md) — kernel convergence
  umbrella; owner: Codex; coordination branch:
  `codex/arch-014-kernel-convergence-program`; blocked on final leaf
  `RUNTIME-187`. Next gate: execute the unblocked ADR-0027 behavior-owner
  leaves and update the convergence scorecard after each retirement.
- [`GRAPHICS-127`](GRAPHICS-127-native-gpu-timestamp-profiler.md) — native GPU
  timestamp profiler; owner: Codex team; branch:
  `codex/graphics-127-gpu-profiler`; in progress. Next gate: repair the RHI
  lifecycle/provenance contract and focused CPU coverage before integrating
  native Vulkan query pools and compiled-pass scopes.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

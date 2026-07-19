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
- [`RUNTIME-180`](RUNTIME-180-extract-camera-module.md) — camera composition
  owner; owner: Codex team; branch:
  `codex/runtime-180-camera-module`; in progress. Next gate: add the dedicated
  typed viewport-input schedule, publish the exact world-bound registry, and
  migrate app-owned reference bootstrap plus omission-safe callers.
- [`GRAPHICS-127`](GRAPHICS-127-native-gpu-timestamp-profiler.md) — native GPU
  timestamp profiler; owner: Codex team; branch:
  `codex/graphics-127-gpu-profiler`; in progress. Next gate: repair the RHI
  lifecycle/provenance contract and focused CPU coverage before integrating
  native Vulkan query pools and compiled-pass scopes.
- [`GRAPHICS-128`](GRAPHICS-128-object-space-normal-bake-shared-index-slice.md) —
  object-space normal bake shared-index slice; owner: Codex team; branch:
  `codex/graphics-128-shared-index-slice`; in progress. Next gate: propagate
  nonzero `FirstIndex` with zero base vertex and run the focused CPU/Vulkan
  evidence.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

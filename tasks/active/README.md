# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`RUNTIME-145`](RUNTIME-145-runtime-frame-path-efficiency-polish.md) —
  runtime frame-path steady-state efficiency polish. Status: in-progress
  Slice A (`StableEntityLookup` event-driven maintenance). Next verification:
  build `IntrinsicRuntimeContractTests` and run the focused
  `SelectionStableLookupComposition` tests.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

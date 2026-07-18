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
- [`HARDEN-086`](HARDEN-086-guarded-hierarchy-query-helpers.md) —
  corruption-aware hierarchy child/descendant queries and two runtime adopter
  migrations; owner: Codex team; branch:
  `codex/harden-086-guarded-hierarchy-queries`. Next gate: shared invariant
  inventory and focused ECS/runtime contract.
- [`RUNTIME-166`](RUNTIME-166-slim-render-extraction-module.md) —
  behavior-preserving RenderExtraction interface/storage partition; owner:
  Codex team; branch: `codex/runtime-166-slim-render-extraction`. Next gate:
  public/private import and declaration inventory.
- [`RUNTIME-179`](RUNTIME-179-extract-async-work-module.md) — app-composed
  async-work ownership plus world-retirement hardening; owner: Codex team;
  branch: `codex/runtime-179-async-work-module`. Next gate: four-axis cohesion
  and live-caller inventory.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

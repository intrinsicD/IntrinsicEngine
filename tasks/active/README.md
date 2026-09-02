# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-154` — Restore PMP curvature parity without normal-seam topology loss](BUG-154-curvature-pmp-parity-corner-normal-topology.md)
  — blocked by `BUG-155` plus its independent fixed-surface review; owns the
  reusable smoothing and authored corner-normal paths.
- [`BUG-156` — Adopt deterministic Framework24 Taubin curvature semantics](BUG-156-curvature-two-ring-smoothing-cancels-features.md)
  — CPU-contracted on `main`; synchronous and queued runtime publication now
  persists all four curvature scalars atomically with stale-state rejection and
  undo/redo. Its independent fixed-surface review remains open.
- [`BUG-162` — Consolidate all local branch history onto main](BUG-162-main-branch-consolidation.md)
  — in progress on `main`; local ancestry and the integrated source surface are
  complete, with the final commit, push, and fetched-head verification pending.
- [`METHOD-039` — Feature-network-constrained curvature patch decomposition](METHOD-039-feature-network-curvature-patch-decomposition.md)
  — paused behind `REVIEW-004`; preserves its completed negative-result surface
  and work-graph history for research resumption after the gate retires.

## Records

Retirement records live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-154` — Restore PMP curvature parity without normal-seam topology loss](BUG-154-curvature-pmp-parity-corner-normal-topology.md)
  — in progress on `main`; fixes the reference smoothing/estimator semantics,
  preserves OBJ normals on the corner domain, and prevents false all-zero
  curvature success.
- [`METHOD-039` — Feature-network-constrained curvature patch decomposition](METHOD-039-feature-network-curvature-patch-decomposition.md)
  — in progress on `main`, owned by `codex-root`; Slice A freezes the practical
  equations, inherited METHOD-038 evidence boundary, supplied-feature seam, and
  generated oracle fixtures before any production selector is added.

## Records

Retirement records live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

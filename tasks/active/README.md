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
- [`BUG-156` — Two-ring support and eigenvalue smoothing cancel genuine curvature](BUG-156-curvature-two-ring-smoothing-cancels-features.md)
  — in progress on `claude/mesh-curvature-analysis-4zvnwi`, owned by `claude`;
  narrows hinge support to one-ring and publishes unsmoothed eigenvalues so
  crease-adjacent curvature is no longer cancelled into zero bands on
  `tests/data/sculpt.obj`; next step is the reporter's Sandbox visual
  confirmation and independent review.
- [`BUG-161` — Clean checkouts cannot validate the Framework24 convergence policy](BUG-161-clean-checkout-framework24-policy-validation.md)
  — in progress on `agent/framework24-product-convergence-goal`, owned by
  `codex-root`; the local regression and strict validators pass, and PR 1030's
  hosted gates are the next verification step.
- [`METHOD-039` — Feature-network-constrained curvature patch decomposition](METHOD-039-feature-network-curvature-patch-decomposition.md)
  — paused behind `REVIEW-004` by the 2026-08-13 product-convergence decision;
  its completed negative-result surface and work-graph history are preserved
  for research resumption after the gate retires.

## Records

Retirement records live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).

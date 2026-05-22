# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GEOM-015`](GEOM-015-gjk-termination-diagnostics.md) — GJK termination
  diagnostics and scale-aware tolerance policy. Promoted from
  `tasks/backlog/geometry/` on 2026-05-22 as the deferred successor to
  GEOM-007 Slice 3.3.d (GEOM-007 retired the same day). Status: in-progress
  (Slice 4 next; Slices 1–3 landed). Slice 1 adds
  `RobustPredicates::ApproxZeroSq` / `ApproxZeroLen` helpers with
  `SignedResult`-shaped diagnostics plus unit tests. Slice 2 migrates the
  original-space magnitude guards in `Geometry.Support` / `Geometry.SDFContact`
  to the new helper with axis-local scale choices and adds regression
  coverage for the previously-flipped sub-millimeter Ellipsoid and
  short-axis Cylinder cases. Slice 3 pins the GJK normalized-workspace
  tolerance contract: keeps `GJK_EPSILON` as a dimensionless constant
  (driver already factors `invScale = 1/shapeScale`), adds a
  `static_assert(GJK_EPSILON > 0 && GJK_EPSILON < 1)`, and documents the
  contract in a new "GJK tolerance contract" subsection of
  `docs/architecture/geometry.md`. Slice 4 (GJK termination-reason
  diagnostics surface + parity / convergence regression battery) is
  queued. Next verification step:
  `cmake --build --preset ci --target IntrinsicGeometryTests`,
  `ctest --test-dir build/ci --output-on-failure -R 'GJK|Support|ContactManifold|Overlap|RobustPredicates' --timeout 60`,
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/docs/check_doc_links.py --root .`, and
  `python3 tools/agents/check_task_policy.py --root . --strict` after each
  slice lands.

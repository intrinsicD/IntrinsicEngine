# Locally Optimal Projection (LOP/WLOP) Point-Cloud Consolidation

Method ID: `geometry.locally_optimal_projection`. Status: **reference**.

Parameterization-free consolidation of noisy, outlier-ridden point clouds:
a projected point set is attracted to the localized L1 median of the input
while a repulsion term keeps it evenly distributed. No normals and no
surface estimation are required. The public API is the geometry module
`Geometry.PointCloud.Consolidation` (`WlopParams`, `Consolidate`); the
paper intake with the verified update equations lives in
[`paper.md`](paper.md).

## Scope and backend status

| Backend | Status | Owning task |
| --- | --- | --- |
| `cpu_reference` | Implemented; correctness tests + smoke benchmark | `METHOD-016` |
| optimized CPU / GPU | Not started (opens only after reference parity) | follow-up |

The sandbox editor exposes the method as the point-cloud
"Processing / Consolidate" window (METHOD-016 slice B): support radius,
repulsion weight, iterations, target percentage, seed, and WLOP/LOP
variant drive the same `Consolidate` call through the undoable
point-cloud replacement path.

## Parameter selection

- **Support radius `h`** — the single most important parameter. Use
  **6-10x the input's average spacing** (the editor auto-derives
  `8 x AverageSpacing` from `Geometry.PointCloud` statistics when left at
  zero, following CGAL's `neighbor_radius` guidance that each projected
  point's neighborhood should cover at least two rings of neighbors).
  Too small fails to regularize and barely denoises; too large shrinks
  points toward the interior of curved surfaces and merges nearby sheets.
  The WLOP paper's own default is `h = 4 * sqrt(d_bb / m)` (bounding-box
  diagonal `d_bb`, input count `m`), which lands in the same regime on
  uniform scans.
- **Repulsion weight `mu`** in `[0, 0.5)` — accuracy/regularity
  trade-off documented in the LOP paper: `0.1-0.3` favors distance
  accuracy, `0.3-0.45` favors an even distribution. The papers and CGAL
  default to `0.45`; the repo correctness fixtures assert denoising at
  `0.3` and uniformity at `0.45`.
- **Iterations** — the projected set settles within tens of iterations
  (CGAL defaults to 35; the papers report 10-100). The convergence
  report exposes per-iteration mean/max displacement so callers can
  judge; the reference default is 20, the fixtures use 12.
- **Target count** — typical consolidation keeps a few percent to ~20%
  of the input (CGAL defaults to 5%).

## Known limitations

- Consolidation only: no normal estimation, no reconstruction.
- Thin structures or surface sheets closer than `h` merge; keep `h`
  below the local feature separation.
- The isotropic weight `theta` ignores sharp features, so edges are
  smeared (the EAR follow-up is the edge-aware variant; out of scope).
- Curved surfaces shrink slightly inward (theta-weighted mean bias,
  grows with `h`); measured `~0.006` absolute on the unit-sphere fixture
  at `h = 0.4`.
- Repulsion acts in full 3D: at `mu` near `0.45` it measurably re-inflates
  the off-surface residual versus `mu = 0.3` (accuracy/regularity
  trade-off above; measured `0.0126` vs `0.0082` mean plane distance on
  the plane fixture).
- Isolated outliers beyond `h` from the surface are simply ignored by
  attraction; a projected point seeded on such an outlier stays there.
  Seed selection (explicit `InitialIndices`) is the caller's control.
- Fixed iteration count, no adaptive termination; the convergence report
  is the caller's signal.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloudConsolidation' --timeout 120
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/benchmark/validate_benchmark_manifests.py
```

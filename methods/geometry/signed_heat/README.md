# Signed Heat Method

Method ID: `geometry.signed_heat`. Status: **reference**.

This package records the paper intake and reference-backend contract for Feng
and Crane's signed heat method. The executable backend lives in the geometry
layer as module `Geometry.SignedHeatMethod`, because it is a reusable
halfedge-mesh operator rather than a standalone runtime subsystem.

## Scope And Backend Status

| Backend | Status | Owning Task |
| --- | --- | --- |
| `cpu_reference` | implemented for surface Variant A | METHOD-002 |

The reference computes per-vertex signed distance from an oriented halfedge
curve on a triangle mesh. The implementation uses existing DEC vertex
mass/cotan operators and `Geometry.Sparse::SparseLDLT`; it is deterministic and
covered by geometry unit tests plus a PR-fast smoke benchmark.

## Known Limitations

- Point-cloud and volumetric variants are not implemented.
- The reference is a vertex-based surface approximation of the paper's
  edge-based Crouzeix-Raviart connection discretization.
- The current analytic tolerance is calibrated on a deterministic flat grid with
  an oriented square boundary (`quality_error_l2 < 0.40`); tighter convergence
  and geometry-central parity baselines are future comparison work.
- Open or duplicated boundary curves are diagnosed as degenerate but still
  produce finite output when the solves succeed.

## Verification

- Correctness: `tests/unit/geometry/Test.SignedHeatMethod.cpp`
- Smoke benchmark: `benchmarks/geometry/manifests/signed_heat_reference_smoke.yaml`

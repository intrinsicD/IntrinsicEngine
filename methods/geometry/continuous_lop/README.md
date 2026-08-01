# Continuous Locally Optimal Projection

Method ID: `geometry.continuous_lop`. Status: **CPU reference**.

This package records the CLOP CPU-reference contract. The executable strategy
extends `Geometry.PointCloud.Consolidation`; it does not create a parallel
method module or backend registry.

## Backend status

| Backend | Status | Owner |
| --- | --- | --- |
| `cpu_reference` | implemented | METHOD-017 |
| optimized CPU | planned | METHOD-019 |
| Vulkan compute | planned | METHOD-020 |

## Selection guidance

CLOP is appropriate when a compact continuous density model is desirable and
the mixture has enough components to resolve the input's geometric scale.
WLOP remains the simpler discrete reference and the parity comparator. Raising
component count preserves finer structure but increases analytic contribution
work; lowering it can blur thin sheets.

## Known limitations

- Ordinary EM is used instead of the paper's hierarchical constrained fit.
- The mixture covariance floor and resolution can bias thin structures.
- The reference intentionally preserves CLOP's published three-Gaussian
  approximation. Stotko, Weinmann, and Klein's 2024 incomplete-gamma kernel
  is a documented accuracy extension, not an unreviewed change to this oracle.
- The reference is serial and makes no throughput claim.

## Verification targets

- Correctness: `tests/unit/geometry/Test.PointCloudConsolidation.cpp`
- Smoke: `benchmarks/geometry/manifests/continuous_lop_reference_smoke.yaml`

# Continuous Locally Optimal Projection

Method ID: `geometry.continuous_lop`. Status: **CPU reference**.

This package records the CLOP CPU-reference contract. The executable strategy
extends `Geometry.PointCloud.Consolidation`; it does not create a parallel
method module or backend registry.

## Backend status

| Backend | Status | Owner |
| --- | --- | --- |
| `cpu_reference` | implemented | METHOD-017 |
| optimized CPU candidate | parity passed; ratio 0.998818 missed the 0.80 gate; not selectable | METHOD-019 |
| Vulkan compute | planned | METHOD-020 |

## Selection guidance

CLOP is appropriate when a compact continuous density model is desirable and
the mixture has enough components to resolve the input's geometric scale.
WLOP remains the simpler discrete reference and the parity comparator. Raising
component count preserves finer structure but increases analytic contribution
work; lowering it can blur thin sheets.

Runtime defaults the shared support radius to the deterministic LOP-family Auto
policy (rank 16, P75, multiplier 1.25), while Manual remains exact. CLOP's
mixture/component work is included in the same contribution guard; see the
[support-radius policy](../../../docs/architecture/support-radius-policy.md).

## Known limitations

- Ordinary EM is used instead of the paper's hierarchical constrained fit.
- The mixture covariance floor and resolution can bias thin structures.
- The reference intentionally preserves CLOP's published three-Gaussian
  approximation. Stotko, Weinmann, and Klein's 2024 incomplete-gamma kernel
  is a documented accuracy extension, not an unreviewed change to this oracle.
- The reference is serial and makes no throughput claim.
- METHOD-019's exact factor-cache/underflow-only candidate remains a
  benchmark validation seam; it does not add a method-manifest backend,
  runtime config token, or UI choice.

## Verification targets

- Correctness: `tests/unit/geometry/Test.PointCloudConsolidation.cpp`
- Smoke: `benchmarks/geometry/manifests/continuous_lop_reference_smoke.yaml`
- Paired candidate comparison:
  `benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`; see
  `../locally_optimal_projection/reports/METHOD-019-result.md`

## Runtime integration

Select `clop` in the registered `sandbox.point_cloud_consolidation` section and
submit through `Extrinsic.Runtime.PointCloudConsolidationService`. Runtime uses
the same CPU-reference strategy, async snapshot/stale-result contract, and
undoable named-property publication as the LOP-family paths. Any finite `vec3`
property on a resolved mesh, graph, or point-cloud element domain can be the
sample set; handle-specific property wrappers and conversions are not required.
Topology-bearing domains use same-cardinality output only. The property-aware
Sandbox panel exposes mixture controls plus resolved-radius, workload,
implementation, and convergence diagnostics.

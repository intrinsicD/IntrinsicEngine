# Edge-Aware Point Set Resampling

Method ID: `geometry.edge_aware_resampling`. Status: **CPU reference**.

This package records the original EAR CPU-reference contract. Executable code
extends `Geometry.PointCloud.Consolidation` and the shared point-cloud kernels;
it does not create a method-owned engine module, registry, or normal service.

## Backend status

| Backend | Status | Owner |
| --- | --- | --- |
| `cpu_reference` | implemented | METHOD-018 |
| optimized CPU candidate | parity passed; ratio 1.037145 missed the 0.80 gate; not selectable | METHOD-019 |
| Vulkan compute | planned | METHOD-020 |

## Selection guidance

- Use isotropic WLOP when the surface is smooth or normals are unavailable and
  edge preservation is not the governing error.
- Use anisotropic WLOP for fixed-count, normal-aware resampling away from
  discontinuities.
- Use EAR when the output count must grow and samples should progressively
  approach a sharp edge with reliable oriented normals.
- Use CLOP when a compact continuous density is the desired attraction model;
  CLOP is not an edge-aware substitute.

## Normal precondition

Authored `p:normal` values must be finite, unitizable, and consistently
oriented. `AuthoredOrEstimate` uses the existing deterministic PCA/MST normal
path when they are absent; `RequireAuthored` fails closed. Refinement occurs on
a method-local copy and never overwrites the source property.

## Known limitations

- Fixed support/normal-angle controls require scale- and noise-aware tuning.
- Open boundaries, large holes, close sheets, severe noise, and ambiguous
  normal orientation are not solved by the original method.
- Later L0, graph/intrinsic, and learned resamplers improve different regimes;
  they are documented comparisons, not silently folded into this oracle.
- The reference is serial and makes no throughput claim.
- METHOD-019's exact local-scan candidate remains a benchmark validation seam;
  it does not add a method-manifest backend, runtime config token, or UI
  choice.

## Verification targets

- Correctness: `tests/unit/geometry/Test.PointCloudConsolidation.cpp` and
  `tests/unit/geometry/Test.PointCloudKernels.cpp`
- Smoke: `benchmarks/geometry/manifests/edge_aware_resampling_reference_smoke.yaml`
- Paired candidate comparison:
  `benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`; see
  `../locally_optimal_projection/reports/METHOD-019-result.md`

The confirmation smoke uses a distinct built-in noisy dihedral cohort and
records the isotropic WLOP contrast, expected-plane error, retained normal
angle, output count, feature-directed insertions, spacing, normal source,
determinism, and fail-closed identity. Its runtime is descriptive only.

## Runtime integration

Select `ear` (or anisotropic `wlop`) in
`sandbox.point_cloud_consolidation`. The runtime operation honors the same
authored-or-estimated normal policy, runs the CPU reference asynchronously,
and publishes pointer-free normal/refinement diagnostics. Input positions and
optional normals are named, count-matched `vec3` properties on any one resolved
element domain; they need not be vertex properties. Same-cardinality output is
undoable on that originating domain. EAR growth is topology-changing and is
therefore accepted only for the topology-free point-cloud point domain through
canonical full-source replacement. `UI-039` owns property-aware Sandbox
discovery; the current PointCloud panel continues to expose the shared
normal-source, angle, refinement, and edge-sensitivity config lane meanwhile.

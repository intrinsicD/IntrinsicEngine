# Edge-Aware Point Set Resampling

Method ID: `geometry.edge_aware_resampling`. Status: **paper intake**.

This package records the original EAR CPU-reference contract. Executable code
extends `Geometry.PointCloud.Consolidation` and the shared point-cloud kernels;
it does not create a method-owned engine module, registry, or normal service.

## Backend status

| Backend | Status | Owner |
| --- | --- | --- |
| `cpu_reference` | in progress | METHOD-018 |
| optimized CPU | planned | METHOD-019 |
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

## Verification targets

- Correctness: `tests/unit/geometry/Test.PointCloudConsolidation.cpp` and
  `tests/unit/geometry/Test.PointCloudKernels.cpp`
- Smoke: `benchmarks/geometry/manifests/edge_aware_resampling_reference_smoke.yaml`

# Locally Optimal Projection

Method ID: `geometry.locally_optimal_projection`. Status: **reference**.

This package records the LOP and WLOP point-cloud consolidation contract. The
executable implementation is the reusable geometry-layer module
`Geometry.PointCloud.Consolidation`; runtime ownership and editor controls are
kept out of the method backend.

## Backend status

| Backend | Strategies | Status | Owner |
| --- | --- | --- | --- |
| `cpu_reference` | LOP, WLOP | implemented | METHOD-016 |

WLOP is the default strategy. Plain LOP follows the same update but uses unit
source/projected density weights.

## Parameter guidance

- `SupportRadius` (`h`) is in input world units. Choose it large enough to span
  local sampling gaps, but below the separation between distinct surface
  sheets. An empty compact-support neighborhood is an explicit failure.
- `RepulsionWeight` (`mu`) is constrained to `[0, 0.5)`. Values near `0` favor
  denoising; values around `0.4`–`0.45` improve spacing.
- `TargetPointCount == 0` preserves the input count. A smaller value uses the
  existing deterministic seeded point-cloud subsampler before projection.
- `ConvergenceTolerance` is an absolute world-unit maximum-displacement bound.
- `MaxInputPointCount` is a caller-controlled allocation guard for the serial
  reference. Requests above it fail with `ResourceLimit` before building an
  index or allocating method work buffers.

## Known limitations

- A single isotropic radius cannot preserve all thin features or strongly
  anisotropic samples. METHOD-018 owns the normal-aware EAR strategy.
- WLOP density correction improves non-uniform sampling but does not infer
  connectivity or reconstruct a surface.
- The reference is serial and correctness-oriented. Optimized CPU and GPU
  backends are separate parity tasks.

## Verification

- Correctness: `tests/unit/geometry/Test.PointCloudConsolidation.cpp`
- Smoke: `benchmarks/geometry/manifests/locally_optimal_projection_reference_smoke.yaml`

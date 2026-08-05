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
| optimized CPU candidate | LOP, WLOP | parity passed; ratios 0.961824 / 0.966845 missed the 0.80 gate; not selectable | METHOD-019 |

WLOP is the default strategy. Plain LOP follows the same update but uses unit
source/projected density weights.

## Parameter guidance

- `SupportRadius` (`h`) is in input world units. Runtime defaults to Auto: the
  selected position property is deterministically k-distance profiled and the
  LOP/isotropic-WLOP policy uses rank 16, P75, and a 1.25 multiplier. Manual
  preserves the configured value exactly. Both modes retain sampled occupancy
  and contribution limits. See the canonical
  [support-radius policy](../../../docs/architecture/support-radius-policy.md).
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
- METHOD-019 retained its exact execution candidates as benchmark-only
  negative evidence. They do not add a backend token, config value, or UI
  choice.

## Verification

- Correctness: `tests/unit/geometry/Test.PointCloudConsolidation.cpp`
- Smoke: `benchmarks/geometry/manifests/locally_optimal_projection_reference_smoke.yaml`
- Paired candidate comparison:
  `benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`; see
  `reports/METHOD-019-result.md`

## Runtime integration

`Extrinsic.Runtime.PointCloudConsolidationModule` exposes LOP and WLOP through
the asynchronous typed operation. The shared validated configuration is the
`sandbox.point_cloud_consolidation` application section. A request names a
finite `vec3` property on any resolved mesh, graph, or point-cloud element
domain; a face-center property is a valid sample set and no `VertexProperty` or
container conversion is required. Same-cardinality output updates named
properties on the originating domain with undo/redo. Mesh/graph count changes
are rejected before scheduling; canonical point-cloud replacement remains the
explicit count-changing path. Support profiling runs from the captured property
on the same job worker before the method; rejected work publishes no geometry.

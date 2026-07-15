# Methods Documentation

This index is the canonical entry point for method/paper implementation documentation.

> **Pathfinder method.** [`METHOD-002 — Signed Heat Method reference backend`](../../tasks/archive/METHOD-002-signed-heat-method-reference-backend.md) is the first method driven end-to-end through the methods pipeline (paper intake → CPU reference → correctness tests → benchmark harness → docs). See retired [`METHODS-001`](../../tasks/archive/METHODS-001-signed-heat-pathfinder.md) for the rationale and dependency chain. Treat the resulting [`methods/geometry/signed_heat/`](../../methods/geometry/signed_heat/) package as the canonical pattern when authoring future method packages.

## Start here

- [Method template](method-template.md)
- [Method manifest schema](method-manifest-schema.md)
- [Reference implementation policy](reference-implementation-policy.md)
- [Backend policy](backend-policy.md)
- [Numerical robustness policy](numerical-robustness-policy.md)
- [Dataset policy](dataset-policy.md)
- [Method figure data export](figure-data-export.md)
- [Report template](report-template.md)

## Related docs

- [Agent method workflow](../agent/method-workflow.md)
- [Methods directory overview](../../methods/README.md)

## Physics Methods

- [`physics.rigid_body_reference`](../../methods/physics/rigid_body_reference/)
  is the deterministic CPU reference backend for fixed-step rigid-body
  integration and first-phase analytic contact contracts. It retires
  [`METHOD-001`](../../tasks/archive/METHOD-001-rigid-body-dynamics-reference-backend.md)
  at `CPUContracted`; CPU-only runtime/world integration is now
  `CPUContracted` by
  [`PHYSICS-001`](../../tasks/archive/PHYSICS-001-physics-world-state-and-runtime-sync.md).
- [`methods/physics/README.md`](../../methods/physics/) records the physics
  method roadmap from
  [`ARCH-002`](../../tasks/archive/ARCH-002-physics-phenomena-roadmap.md). The
  first non-rigid follow-ups are
  [`METHOD-009`](../../tasks/archive/METHOD-009-particle-spring-reference-backend.md),
  [`METHOD-010`](../../tasks/archive/METHOD-010-xpbd-cloth-shell-reference-backend.md),
  and [`METHOD-011`](../../tasks/archive/METHOD-011-sph-fluid-reference-backend.md).
  Each remains CPU-reference-first; GPU backends require later parity-proven
  tasks.

## Geometry Methods

- [`geometry.boundary_first_flattening`](../../methods/geometry/boundary_first_flattening/)
  is the CPU-reference method contract for Sawhney and Crane's Boundary
  First Flattening (ACM TOG 37(1), Article 5, 2017; DOI
  [`10.1145/3132705`](https://doi.org/10.1145/3132705)). Its bounded
  `METHOD-023` scope accepts an already-cut triangle disk and defines
  `AutomaticConformal`, approximate `TargetLengths`, and `TargetAngles` modes.
  Cones, cutting, optimized/GPU backends, and performance claims are excluded.
  The executable CPU reference is exposed through
  `Geometry.Parameterization.Bff`, with analytic correctness tests and a
  manifest-backed smoke benchmark; `RUNTIME-176` owns later config
  serialization and selected-mesh UV writeback.
- [`geometry.signed_heat`](../../methods/geometry/signed_heat/) is the
  deterministic CPU reference backend for Feng & Crane's Signed Heat Method on
  triangle-mesh surfaces. Given an oriented halfedge curve, it writes
  per-vertex signed distance/source properties, reports structured diagnostics,
  and uses `Geometry.Sparse::SparseLDLT` over the existing DEC vertex
  mass/cotan operators. It is delivered by
  [`METHOD-002`](../../tasks/archive/METHOD-002-signed-heat-method-reference-backend.md)
  at `CPUContracted`; correctness lives in
  [`Test.SignedHeatMethod.cpp`](../../tests/unit/geometry/Test.SignedHeatMethod.cpp)
  and the smoke benchmark in
  [`signed_heat_reference_smoke.yaml`](../../benchmarks/geometry/manifests/signed_heat_reference_smoke.yaml).
- [`geometry.progressive_poisson`](../../methods/geometry/progressive_poisson/)
  is the deterministic CPU reference backend for progressive Poisson-disk
  subsampling via phase-parallel spatial hashing: given an unordered point set
  in R^d (d in {2,3}) it computes a progressive ordering of an accepted subset
  such that every prefix `[0,k)` is a Poisson-disk sampling at its hierarchy
  level. It is delivered by
  [`METHOD-012`](../../tasks/archive/METHOD-012-progressive-poisson-disk-cpu-reference.md)
  at `CPUContracted`; correctness lives in
  [`Test.ProgressivePoissonReference.cpp`](../../tests/unit/geometry/Test.ProgressivePoissonReference.cpp)
  and the smoke benchmark in
  [`progressive_poisson_reference_smoke.yaml`](../../benchmarks/geometry/manifests/progressive_poisson_reference_smoke.yaml).
  The Vulkan-compute GPU backend contract slices are retired in
  [`METHOD-013`](../../tasks/archive/METHOD-013-progressive-poisson-disk-gpu-backend.md):
  requested-vs-actual backend diagnostics, CPU fallback for
  `gpu_vulkan_compute` requests, recordable Vulkan shader/layout dispatch
  seams, runtime-owned SoA uploads, readback-copy targets, parsed readback
  payloads, and CPU-reference parity diagnostics are in place. Public GPU
  execution, `gpu;vulkan` parity tests, and benchmark metric extension are owned
  by open follow-up
  [`METHOD-014`](../../tasks/backlog/methods/METHOD-014-progressive-poisson-gpu-operational-parity.md).

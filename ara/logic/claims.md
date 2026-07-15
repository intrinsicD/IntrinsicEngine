# Claims

## C01: PR-fast ccache provides a material same-shape latency reduction
- **Statement**: For the CI-007 `pr-fast` gate shape at commit `5394e51b`, five
  warm ccache samples reduced build median/p95 by 56.7%/58.3% and total
  median/p95 by 47.6%/50.0% versus five contemporary cold samples, while every
  gate and hermetic parity probe passed with zero ccache errors.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A same-shape repeat cohort reports no warm median
  build improvement, a regressed warm build p95, nonzero ccache errors, or a
  cached/clean correctness mismatch.
- **Proof**: [ara/evidence/tables/ci007_ccache_cohort.md,
  tasks/archive/CI-007-module-safe-persistent-ccache-pilot.md,
  docs/benchmarking/ci-policy.md]
- **Dependencies**: []
- **Tags**: CI, ccache, C++23 modules, gate latency
- **From staging**: O15

## C02: Bounded disk BFF supports three deterministic CPU reference controls
- **Statement**: On supported triangle disks, the METHOD-023 CPU reference
  deterministically produces finite UVs for automatic conformal, approximate
  target-boundary-length, and prescribed exterior-angle controls, reports
  residual diagnostics, and fails closed for unsupported or invalid input.
- **Status**: supported
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: A supported deterministic fixture produces
  non-finite or nondeterministic UVs, a declared control fails its residual or
  orientation contract, or unsupported topology/input returns a UV payload.
- **Proof**: [tasks/done/METHOD-023-boundary-first-flattening-reference-backend.md,
  tests/unit/geometry/Test.BoundaryFirstFlattening.cpp,
  src/geometry/Geometry.Parameterization.Bff.cpp,
  commit 4bf4f67b]
- **Dependencies**: [K14, K15]
- **Tags**: geometry, parameterization, BFF, CPU reference
- **From staging**: O30

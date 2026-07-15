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

## C03: Adaptive Delaunay/QEF meshing is a gated research hypothesis
- **Statement**: QEF-derived feature samples may improve an adaptive Delaunay
  implicit-meshing reference on thin and sharp-feature fixtures, but the broad
  combination is prior-art constrained and does not warrant a 3D engine
  implementation unless a deterministic adversarial 2D falsifier passes first.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: The 2D adversarial study fails its manifoldness,
  termination, feature-retention, or field-evaluation gates, or prior-art
  comparison leaves no distinct evidence question worth testing.
- **Proof**: [N220,
  tasks/backlog/methods/METHOD-027-adaptive-delaunay-qef-implicit-meshing.md]
- **Dependencies**: []
- **Tags**: geometry, implicit meshing, Delaunay, QEF, falsifier
- **From staging**: O39

## C04: Confidence-driven subdivision may improve guided Walk on Stars
- **Statement**: After a canonical METHOD-004 CPU reference exists, spatial
  subdivision driven by contribution and direction confidence may improve the
  variance-memory frontier of guided Walk on Stars, as a provisional transfer
  rather than a claim of a new guiding family.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Matched deterministic fixtures show no variance
  benefit at equal sample and memory budgets, confidence adds unacceptable
  bias, or the measured behavior is already fully explained by cited guided-WoSt
  prior art.
- **Proof**: [N220,
  tasks/backlog/methods/METHOD-028-confidence-driven-walk-on-stars-guiding.md]
- **Dependencies**: []
- **Tags**: Monte Carlo PDE, Walk on Stars, guiding, confidence, memory
- **From staging**: O40

## C05: Invariant-aware mip objectives may preserve scientific fields better
- **Statement**: At equal storage, field-specific optimization objectives may
  reduce angular error, categorical bleed, or isoline drift in normal, label,
  and signed-scalar mip pyramids relative to raw-channel averaging.
- **Status**: hypothesis
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Falsification criteria**: Deterministic CPU fixtures show no material
  improvement on the declared invariant metrics, introduce unacceptable
  reconstruction artifacts, or exceed the task's storage and build-cost limits.
- **Proof**: [N220,
  tasks/backlog/geometry/GEOM-065-invariant-aware-scientific-field-mip-pyramids.md]
- **Dependencies**: []
- **Tags**: geometry, scientific fields, mipmaps, invariants, visualization
- **From staging**: O45

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
  tasks/done/CI-007-module-safe-persistent-ccache-pilot.md,
  docs/benchmarking/ci-policy.md]
- **Dependencies**: []
- **Tags**: CI, ccache, C++23 modules, gate latency
- **From staging**: O15

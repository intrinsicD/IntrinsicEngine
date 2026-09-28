---
id: GEOM-112
theme: I
depends_on: [GEOM-111]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; implementation owes parity tests against the CUDA host references.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-112 — Complete CPU port of phase-parallel progressive Poisson sampling

## Goal
- Port the remaining options of the operator's CUDA sampler (`code/progressive_poisson.{h,cu}`)
  into `Geometry.PointSampling`'s Poisson method: `CellSelectionPolicy` Bounded (the fast
  profile, with `max_cell_retries` and `repair_coarse_levels`), BestOfCandidates
  (`candidate_budget`), FeaturePriority (priority scores, two bands), `exhaustive_coarse_levels`,
  randomized phase order, `WithinLevelOrdering::SpatiallyBalanced` (Morton + bit reversal),
  `reorder_within_levels`, order-only mode (no splat radii), and the named profiles
  (Fast, Balanced, Quality, HAPDS).

## Acceptance criteria
- [ ] Every option reachable through `PointSampling::Params`; level-boundary min-distance guarantee kept for each policy that promises it.
- [ ] Parity with the CUDA sampler's host-side contract (`test_progressive_poisson.cu --host-only` fixtures) where the CPU order is defined; documented where GPU parallel acceptance differs.
- [ ] Profiles and options exposed in the sampling config (RUNTIME-289).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling|ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

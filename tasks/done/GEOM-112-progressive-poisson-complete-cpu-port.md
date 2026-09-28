---
id: GEOM-112
theme: I
depends_on: [GEOM-111]
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; evidence is the parity and guarantee tests in Test.ProgressivePoissonReference.cpp and Test.PointSampling.cpp.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-112 — Complete CPU port of phase-parallel progressive Poisson sampling

## Completion — 2026-09-28
Commit: the GEOM-112 commit on `claude/cpd-nystrom` (follows `7eab15f64`). Maturity reached: `CPUContracted`. Every CUDA
sampler option runs on the CPU with candidate lists in remaining order; the default
(Exhaustive) output is bitwise unchanged (checked against the previous build on six
fixtures); SpatiallyBalanced reproduces the CUDA test sequences exactly; every policy keeps
the level-boundary guarantee. Next maturity owners: RUNTIME-289/274 (config and UI),
METHOD-014 (Vulkan parity).

## Goal
- Port the remaining options of the operator's CUDA sampler (`code/progressive_poisson.{h,cu}`)
  into `Geometry.PointSampling`'s Poisson method: `CellSelectionPolicy` Bounded (the fast
  profile, with `max_cell_retries` and `repair_coarse_levels`), BestOfCandidates
  (`candidate_budget`), FeaturePriority (priority scores, two bands), `exhaustive_coarse_levels`,
  randomized phase order, `WithinLevelOrdering::SpatiallyBalanced` (Morton + bit reversal),
  `reorder_within_levels`, order-only mode (no splat radii), and the named profiles
  (Fast, Balanced, Quality, HAPDS).

## Acceptance criteria
- [x] Every option reachable through `PointSampling::Params`; level-boundary min-distance guarantee kept for each policy that promises it.
- [x] Parity with the CUDA sampler's host-side contract (`test_progressive_poisson.cu --host-only` fixtures) where the CPU order is defined; documented where GPU parallel acceptance differs.
- [x] Profiles and options exposed in the sampling config — deferred by design to RUNTIME-289 (config/UI owner), which lists them.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling|ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

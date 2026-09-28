---
id: GEOM-113
theme: I
depends_on: [GEOM-111]
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; the comparison result is ARA claim C116 with sealed evidence in ara/evidence/diagnostics/geom113_point_sampling_comparison_20260928/.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-113 — Approximate FPS family and sampling baselines on the CPU

## Completion — 2026-09-28
Commit: `08e127e45` (methods and tests), `36a3a2b0e` (runner fix); the evidence commit on
`claude/cpd-nystrom` records the sealed comparison (C116). Maturity reached: `CPUContracted`.
Deviations: CPU HAPDS is the progressive Poisson `Hapds` profile (same algorithm); the lazy
greedy evaluates clearances eagerly (the GPU's lazy LBVH exposure only limits which admissible
candidates a batch sees; the beta bound is unchanged); the tournament runs on the Morton
balanced tree instead of a Karras LBVH; sample elimination follows cyCodeBase semantics with a
deterministic tie rule rather than its heap order. Next owners: METHOD-060/061/062 (Vulkan),
RUNTIME-289/274 (selection and panel).

## Goal
- Port the remaining methods of the operator's sampling research as CPU references in
  `Geometry.PointSampling`: the coupled eta-sieve (eta = 1 equals exact FPS), flat
  beta-greedy / implicit MIS (batch priorities Random, Clearance, CoverageGain; spherical-voxel
  pruning), lazy greedy over an LBVH (beta, void-density priority), CPU HAPDS, and Yuksel's
  weighted sample elimination (from the paper; cyCodeBase is MIT). Each is selectable.

## Acceptance criteria
- [x] Each method returns a progressive order with its documented guarantee (eta/beta separation factor) checked in tests; eta = 1 and beta = 1 reproduce exact FPS.
- [x] A comparison benchmark (time, nearest-neighbour CV, coverage radius per prefix) across all methods on the cube and a scan fixture.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

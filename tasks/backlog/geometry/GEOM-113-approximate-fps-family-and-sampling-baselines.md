---
id: GEOM-113
theme: I
depends_on: [GEOM-111]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; implementation owes tests and a comparison benchmark.
contract_schema: 1
contracts: [repo.source-documentation]
---
# GEOM-113 — Approximate FPS family and sampling baselines on the CPU

## Goal
- Port the remaining methods of the operator's sampling research as CPU references in
  `Geometry.PointSampling`: the coupled eta-sieve (eta = 1 equals exact FPS), flat
  beta-greedy / implicit MIS (batch priorities Random, Clearance, CoverageGain; spherical-voxel
  pruning), lazy greedy over an LBVH (beta, void-density priority), CPU HAPDS, and Yuksel's
  weighted sample elimination (from the paper; cyCodeBase is MIT). Each is selectable.

## Acceptance criteria
- [ ] Each method returns a progressive order with its documented guarantee (eta/beta separation factor) checked in tests; eta = 1 and beta = 1 reproduce exact FPS.
- [ ] A comparison benchmark (time, nearest-neighbour CV, coverage radius per prefix) across all methods on the cube and a scan fixture.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

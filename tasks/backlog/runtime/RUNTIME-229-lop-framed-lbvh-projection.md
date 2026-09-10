---
id: RUNTIME-229
theme: J
depends_on: [RUNTIME-228]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive follow-up records deferred framed GPU work; no implementation or capability claim.
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-229 — Framed LOP LBVH projection

## Goal
Wire the RUNTIME-228 seed/init/step arithmetic to framed Vulkan LBVH neighborhoods, preserving the existing grid backend.

## Context
The 2026-09-10 overnight sequence first establishes CPU reference agreement and the cache adapter. This follow-up owns actual GPU integration; no GPU LBVH projection token is implemented yet.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Existing finite float3 source/projected spans and complete PointNeighborhoods. |
| Compatible entity sources | All eight compact canonical element domains. |
| RuntimeModule | PointCloudConsolidationModule, SpatialIndexCache and JobService. |
| Config/agent | Add an explicit vulkan_lbvh token and shared batch/capacity controls only with implementation. |
| UI | Existing consolidation selector and validated config path. |
| Publication | Existing atomic named output or explicit pointcloud replacement/history. |
| End-to-end tests | Actual multi-iteration GPU/reference comparisons, cache reuse, stale/cancel/reject/overflow and history. |

## Spatial acceleration review
Lease one fixed source cache entry. Gather initial seed-to-source attraction rows. For each iteration create and retain a private moving-sample workspace; query projected-to-source attraction without ID exclusion and projected-to-projected repulsion with self exclusion. Complete conservative radius candidates feed exact CPU support/reduction. Overflow rejects without publication. Preserve normal-or-zero coordinates and radius limits used by density adapters until general GPU denormal support is separately proved. Do not truncate or replace the existing grid without comparison evidence.

## Acceptance criteria
- [ ] Framed jobs retain source/workspace leases through terminal completion and cancellation.
- [ ] Partial submission cannot race worker state or publish incomplete results.
- [ ] Three moving iterations and downsampled cross-domain IDs agree with reference on all eight domains.
- [ ] Actual GPU benchmark and failure tests pass; register framed CTest timeout explicitly.
- [ ] Docs, requested/actual diagnostics and follow-up WLOP/CLOP/EAR reminders stay synchronized.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^PointCloudConsolidation' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicRuntimePointCloudConsolidationGpuParityTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PointCloudConsolidation' --timeout 120
```

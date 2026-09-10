---
id: RUNTIME-229
theme: J
depends_on: [RUNTIME-228]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T04:31:21Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# RUNTIME-229 — Framed LOP LBVH projection

## Goal
Wire the RUNTIME-228 seed/init/step arithmetic to framed Vulkan LBVH neighborhoods, preserving the existing grid backend.

## Context
The operator authorized sequential spatial consumers until 07:00 Europe/Berlin. RUNTIME-228 established CPU reference agreement and the cache adapter. This slice adds the framed GPU path within that authorization.

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

## Non-goals
No WLOP/CLOP/EAR adapter, new projection formula, automatic backend/default change, shader reduction or replacement of the existing grid backend.

## Right-sizing
Reuse SpatialIndexCache workspaces, JobService dependency/finalization and the RUNTIME-228 geometry steps. Cross-set radius pagination stays private to PointCloudConsolidationModule because attraction and repulsion have different source cardinalities and exclusions. No additional service or virtual backend interface.

## Slice plan
One slice: bounded config and framed query stages, shared CPU projection updates, actual GPU and CPU/config tests, benchmark, documentation and independent review.

## Required changes
- [x] Add explicit Vulkan LBVH config/UI eligibility with bounded batches, capacity and iteration count.
- [x] Implement leased source and moving workspace queries with one final publication.

## Tests
- [x] CPU config/device rejection and actual GPU domain/reuse/reference/overflow/stale/cancel cases pass.
- [x] Full CPU and actual GPU checks pass without new backend skips.

## Docs
- [x] Synchronize the spatial inventory, method/benchmark and bounded capability record.

## Acceptance criteria
- [x] Framed jobs retain source/workspace leases through terminal completion and cancellation.
- [x] Partial submission cannot race worker state or publish incomplete results.
- [x] Three moving iterations agree with reference on all eight domains; point-cloud downsampling covers different source/query IDs while mesh/graph cardinality stays fixed.
- [x] Actual GPU benchmark and failure tests pass; register framed CTest timeout explicitly.
- [x] Docs, requested/actual diagnostics and follow-up WLOP/CLOP/EAR reminders stay synchronized.

## Verification
```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^PointCloudConsolidation' --timeout 60
cmake --preset ci-vulkan
CCACHE_DISABLE=1 cmake --build --preset ci-vulkan --target IntrinsicRuntimePointCloudConsolidationGpuParityTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'PointCloudConsolidation' --timeout 180
```

## Forbidden changes
No silent fallback or truncation, cross-domain ID exclusion, new layer exceptions, or unsupported speedup/general device claims.

## Review and evidence limits
The partial-submission branch is covered by independent static ownership review, not an injected JobService submission failure in this module. The testable editor-command adapter used by other consumers is not present here. No injected partial-submission test claim will be made. Failure cases assert exact status, unchanged output and unchanged history; downsampling and early convergence have explicit CPU comparisons and history transitions.

The initial full-CPU invocation stopped during discovery because the new timeout guard used `if(TEST ...)` without CMP0064 in the generated CTest include. The guard now follows the existing discovered-producer-variable pattern; the failed receipt remains as diagnosis evidence.

The first new GPU-case attempt incorrectly tested operational readiness during cold initialization, before recipe validation promotes the device, and skipped. The test now uses bootstrap readiness only for capability skip and requires operational execution during frames, matching the existing cases. Both existing grid cases passed on that initial invocation; only the corrected new case needs rerun.

The first operational run matched CPU positions on all eight cold and repeated domains, but its warm-cache assertion assumed publication retained input property storage. Existing history publication replaces storage and invalidates prior property revisions. The warm phase now explicitly primes the current source storage before acquisition, as in the CPU fixture. Eighteen successful projections plus three failure requests exceeded the original 95-second watchdog; the unchanged workload receives 150 seconds internally and a 180-second CTest limit. This does not assert cache reuse across storage replacement.

A final test-only rebuild attempted CMake regeneration inside the sandbox and hit the known vcpkg egress restriction tracked by BUG-065. Native preset regeneration is the verification path; the failed command is preserved and excluded from successful gate receipts.

## Completion
**Completed:** 2026-09-10
**Commit:** `caa85ce781be772faf30de920322010825413afa`

C90 and [bound verification](../../ara/evidence/tables/lop_vulkan_lbvh_verification_2026-09-10.md) record 48 focused cases, 4465 distinct CPU passes and 3 actual Vulkan checks. Partial-submission safety is statically reviewed only. WLOP/CLOP/EAR remain separate spatial-inventory follow-ups.

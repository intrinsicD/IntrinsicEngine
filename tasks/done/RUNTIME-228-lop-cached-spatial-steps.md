---
id: RUNTIME-228
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T04:02:49Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration, repo.source-documentation]
maturity_target: Operational
---
# RUNTIME-228 — Cached LOP spatial steps

## Goal
Separate LOP neighborhood gathering from the existing projection arithmetic and wire cached CPU LBVH into the existing runtime/config/UI path.

## Context
The operator authorized sequential spatial consumers and Framework24 ports until 07:00 Europe/Berlin on 2026-09-10. This bounded first LOP adapter slice precedes its framed Vulkan adapter; the current Vulkan grid remains available.

## Formulation and references
Reviewed the original [Lipman et al. 2007 technical report](https://www.wisdom.weizmann.ac.il/~ylipman/lop/LOP_final_TR.pdf), DOI 10.1145/1276377.1276405, and existing WLOP/CLOP intakes. Preserve the engine's theta-weighted L2 initialization, compact theta LOP kernel, WLOP-family linear repulsion and 0.01h distance floor. This is the existing engine formulation, not a claim of the original inverse-cubic repulsion or unchanged asymptotic guarantees.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Finite float3 spans and canonical CSR candidate neighborhoods. |
| Compatible entity sources | Existing eight compact element domains; no new provenance restriction. |
| RuntimeModule | Existing PointCloudConsolidationModule and SpatialIndexCache. |
| Config/agent | Existing consolidation section adds cpu_lbvh, eligible for LOP only. |
| UI | Existing shared consolidation backend selector. |
| Publication | Existing named output and explicit pointcloud replacement/history path. |
| End-to-end tests | CPU reference agreement, runtime publication/history and config tests. |

## Spatial acceleration review
Retain one immutable cached source index; build a private moving-sample index for each projection iteration. Attraction rows index source samples and must not exclude a same-numbered projected ID. Repulsion rows index projected samples and exclude their own ID in the reducer. Complete ascending unique candidate rows, conservative float radius and exact double kernel filtering preserve support; no truncation. RUNTIME-229 owns framed GPU queries, moving workspace leases, overflow/cancellation tests and actual GPU evidence. WLOP/CLOP/EAR adapters remain in the spatial-consumer inventory.

## Right-sizing
Reuse the existing L2 and iteration bodies, PointNeighborhoods, PointLBVH, runtime snapshot/job/publication/history and config. Four free functions expose deterministic seed/init/step/cached execution. No new service, backend interface or lifecycle module.

## Non-goals
No GPU LBVH driver, WLOP/CLOP/EAR adapter, new projection formula, deleted-slot eligibility expansion or METHOD-019 performance promotion.

## Slice plan
One slice: supplied-neighborhood reference seam and CPU adapter, correctness tests and benchmark manifest, runtime/config/UI, verification and independent review.

## Required changes
- [x] Shared neighborhood-driven LOP seed/initialization/iteration and cached CPU runner.
- [x] Runtime cache lease and honest backend diagnostics with shared config/UI.

## Tests
- [x] Complete candidate rows and cached index agree with reference through moving iterations and downsampling.
- [x] Malformed rows, mismatched source identity and unsupported strategies fail closed.
- [x] Tiny support, config, publication/history and existing CPU suite pass.

## Docs
- [x] Synchronize method/spatial inventory and record GPU follow-up.

## Acceptance criteria
- [x] New selectable CPU backend has reference tests and benchmark evidence without a speedup claim.
- [x] Existing arithmetic, publication and Vulkan grid behavior remain passing.
- [x] Full CPU, structural and independent fixed-surface review pass.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R '^PointCloudConsolidation' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
No new GPU token, silent fallback, altered projection formula, layer exception or performance claim.

## Completion
**Completed:** 2026-09-10
**Commit:** `020622c844d1809e4614d5ed120925e63aa3bdad`

C89 and [bound verification](../../ara/evidence/tables/lop_cpu_lbvh_verification_2026-09-10.md) record 46 focused cases, 4463 distinct CPU passes and two existing Vulkan grid checks. CPU LBVH is explicit; framed GPU LBVH remains RUNTIME-229.

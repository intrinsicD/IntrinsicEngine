---
id: BUG-185
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-10T03:46:20Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
maturity_target: Operational
---
# BUG-185 — Preserve tiny support in workload occupancy guards

## Goal
Correct support-radius occupancy so float query underflow cannot omit strict double-support contributors and admit an unsafe neighborhood budget.

## Context
The operator authorized sequential spatial consumers until 07:00 Europe/Berlin on 2026-09-10. During preparation for the LOP adapter, read-only review identified the old float-only broad-phase expansion in Geometry.SupportRadius, separate from the corrected density kernels. Two normal-coordinate fixtures reproduce missed contributors; an extremely small double radius also underflows the existing squared cutoff and omits coincident samples.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Existing finite float3 span and radius/policy/budget values. |
| Compatible entity sources | Existing canonical-domain callers; no provenance restriction added. |
| RuntimeModule | Existing PointCloudConsolidationModule keeps its analysis call and budget/publication behavior. |
| Config/agent | Existing support-radius modes and workload controls; no new token or tuning state. |
| UI | Existing shared consolidation panel uses the same analysis and diagnostics. |
| Publication | Analysis values only; unsafe support rejects the existing operation before geometry publication. |
| End-to-end tests | Geometry regression plus existing consolidation/runtime/full CPU checks. |

## Spatial acceleration review
Reuse the conservative query-radius helper added for compact density weights. Keep exact double Euclidean strict support in the occupancy narrow phase, including self and distinct coincident samples. CPU KD-tree remains the index owner; automatic rank selection, GPU query adapters and full-neighborhood worst-case bounds remain separate work. The workload estimate is sampled P95 occupancy, not a claim of exhaustive global occupancy.

## Non-goals
No new selectable backend, GPU adapter, sampling policy, quantile/rank formulation, budget change or layer exception.

## Slice plan
One slice: reproduce both query-underflow and squared-cutoff-underflow omissions, reuse conservative candidates and robust strict distance comparison, verify and retire.

## Required changes
- [x] Reuse conservative float broad-phase radius.
- [x] Avoid double support-square underflow in exact occupancy.

## Tests
- [x] Two- and 33-point normal-coordinate fixtures reject neighbor budgets of one and 32 with correct occupancy and predicted counts.
- [x] Coincident samples remain inside a positive 1e-310 support radius.
- [x] Existing support-radius, consolidation, density and full CPU checks pass.

## Docs
- [x] Record the bounded correction and preserve remaining projection/query-adapter reminders.

## Acceptance criteria
- [x] Regressions fail before the correction and pass afterward.
- [x] Exact occupancy and workload guards retain their strict support and sampled-estimate contract.
- [x] Full CPU, structural and independent fixed-surface review pass.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R '^(SupportRadius|PointCloudKernels|PointCloudConsolidation)\.' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
Do not weaken occupancy budgets, introduce truncation or claim a GPU implementation.

## Reproduction and completion

**Completed:** 2026-09-10
**Commit:** `e3ac231d8fc709e0a83233e07ebf5eb0c1fe10a9`

Both regression tests failed before correction. The two-point and 33-point fixtures previously passed budgets that should reject; the 1e-310 coincident fixture incorrectly reported zero support and zero predicted work. Conservative candidates and direct double-distance comparison restore the declared strict occupancy predicate. All 55 focused geometry cases pass, and full CPU selects 4458 tests with no failures; five native follow-ups yield 4457 distinct passes and one expected unsanitized leak-control skip. No new GPU execution or performance claim is made. The high-risk report retains failing before-fix receipts and the passing replacement gates; independent review and seal bind this correction.

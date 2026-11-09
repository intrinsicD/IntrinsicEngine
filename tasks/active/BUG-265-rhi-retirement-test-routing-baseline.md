---
id: BUG-265
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: deterministic test inventory correction; no performance claim
contract_schema: 1
contracts: []
contract_review: Existing test-routing baseline and counts only; no runtime, layering, method or configuration changes.
---
# BUG-265 — Include the new RHI retirement cases in the routing baseline

## Operator direction (verbatim)
1. "Merge nach Main und push zu Remote"
2. "Hast du das pr Review Ereignis gesehen?"

## Goal
Complete the ARCH-024 test-registration update before merging PR #1047. Its new RHI tests
were registered and built but omitted from the existing fixed BUG-106 case inventory.

## Observed failure and correction
At `dfcc84efa`, [UBSan job 129069528863](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/42590091392/job/129069528863)
built successfully, then reconciliation reported two extra cases in IntrinsicGraphicsUnitTests:
`RHIBufferManagerScratch.DefaultFrameRetirementRefusesDeviceLoss` and
`RHIBufferManagerScratch.DeviceRetirementRecyclesScratchWhileTheFrameCounterIsFrozen`.
The same reconciliation failed locally. Add those exact identities and update the existing
per-target count from 31 to 33 and total count from 192 to 194. Keep every existing case,
selection rule, label, missing/extra-case check and sanitizer setting unchanged.

## Acceptance criteria
- [ ] Independent review accepts the exact two-case baseline/count correction.
- [ ] Live reconciliation passes with the new cases included; the existing self-tests remain green.
- [ ] Fresh final-head CPU/sanitizer CI passes routing reconciliation and executes its selected tests.

## Verification
```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
The live local check passes after the correction (28 targets, 6,250 registered cases, 439 sources);
all 20 routing self-tests pass. Those counts describe routing, not executed C++ tests or
sanitizer results. Temporary logs are retained by active ARCH-024.

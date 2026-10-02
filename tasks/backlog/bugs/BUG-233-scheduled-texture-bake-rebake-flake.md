---
id: BUG-233
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up found in the RUNTIME-315 full CPU gate; evidence is the diff, tests, and CI
contract_schema: 1
contracts: []
contract_review: A scheduling race inside the texture-bake runtime module; no catalog contract covers bake job scheduling.
---
# BUG-233 — A repeated scheduled texture bake intermittently fails

## Goal
- Re-baking the same output on the same entity right after an earlier bake succeeds
  deterministically, whatever the shared scheduler's progress on the earlier bake's run job.

## Context
- Found 2026-10-02 by the RUNTIME-315 full CPU gate (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine' -j$(nproc)`).
  `AssetWorkflowModule.CallerOwnedBakeReconciliationIsAtomicAndPreservesUnrelatedChannels`
  (`tests/contract/runtime/Test.AssetWorkflowModule.cpp`) fails about 1 run in 5-30:
  `baker->Bake(albedoRequest).Succeeded()` is false at line 1152 (the per-kind re-bake loop,
  trace kind 3 = UInt32). It passes in isolation most of the time.
- Suspected cause: UI-073 slice 1 (`8c1ee6e89`) gives every scheduled bake a run job submitted
  to the shared scheduler (`SchedulerScope` in the test); a re-bake issued while the previous
  run job of the same output is still in flight is refused or loses its record. RUNTIME-315
  does not touch the bake path.

## Control surfaces
- None; behavior of the existing `TextureBakeService::Bake` path.

## Slice plan
1. Reproduce under load (`--repeat until-fail`), root-cause at the bake owner, fix, add a test
   that fails before the fix, and run the test 200 times under load.

## Acceptance criteria
- [ ] The re-bake path is deterministic: a regression test that drives a re-bake while the previous run job is in flight fails before the fix and passes after.
- [ ] `CallerOwnedBakeReconciliationIsAtomicAndPreservesUnrelatedChannels` passes 200 consecutive runs under parallel load.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci -R 'AssetWorkflowModule|TextureBakeModule' --repeat until-fail:200 -j8 --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

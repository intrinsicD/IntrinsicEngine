---
id: BUG-212
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive test scheduling repair with retained regression failure and CTest metadata verification
contract_schema: 1
contracts: []
contract_review: Existing CTest processor accounting only; no engine implementation, test behavior or verification-policy change.
---
# BUG-212 — Account for two clustering test worker pools

## Goal

Fix the pre-existing worker-budget inventory mismatch found while verifying
[PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044). Two clustering
tests use the existing two-worker engine configuration but were omitted from
the CTest processor-budget table and its exact inventory regression. Their
source is unchanged from the PR parent branch. Each needs three slots under
the existing scheduler accounting rule.

## Acceptance criteria

- [x] Declare three processor slots for both tests in the canonical table.
- [x] Update the exact regression inventory and retain the original failure.
- [x] Confirm generated CTest metadata and execute both tests successfully.

## Verification

```bash
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
cmake --preset ci
ctest --test-dir build/ci --show-only=json-v1
ctest --test-dir build/ci --output-on-failure -R '^ClusteringModule\.(EveryPointDomainPreservesDeletedSlotsAndExactScalarUndo|UnrepresentableScalarLabelRejectsAllOutputPublication)$' --timeout 60 -j1
```

The [original regression failure](../evidence/BUG-212/workflow-concurrency-before.log)
records the missing declarations. Only scheduling metadata and its exact test
inventory change; the engine and C++ test sources remain unchanged.

## Completion

Retired 2026-09-23 at the CI-maintenance endpoint.
PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044),
the enclosing CI prerequisite and scheduling repair commit.

The exact inventory regression passes all 20 cases. The ci preset configured and rebuilt IntrinsicRuntimeContractTests successfully; generated CTest metadata assigns three slots to both cases and both tests pass. Actual Claude Opus 5.5 reviewed the source/table correspondence and unchanged guards. No C++ source or test behavior changed.

[Generated budgets](../evidence/BUG-212/generated-processor-budgets.json),
[CTest result](../evidence/BUG-212/ctest.log), and
[final Claude review](../evidence/BUG-212/claude-ci-final-review.md) retain the proof.
The review preceded the final local build/test completion; the subsequent
metadata and execution records close those pending local items.

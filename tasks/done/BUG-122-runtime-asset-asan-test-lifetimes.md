---
id: BUG-122
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive documentation-only supersession closure; implementation and sanitizer evidence belong to RUNTIME-200.
contract_schema: 1
contracts: []
contract_review: Reviewed the contract catalog; this change records supersession without changing task policy, source APIs, runtime behavior, or geometry contracts.
---
# BUG-122 — Runtime asset ASan tests retain expired callback and snapshot state

## Status

- Retired on 2026-09-05 as superseded by
  [RUNTIME-200](RUNTIME-200-staged-asset-import-materialization-recipe.md).
- Commit: superseding accepted implementation through `c974242b`; handoff-test
  removal in `0fdeac008`.
- This is a documentation-only closure, not a new lifetime repair or a claim
  that the four historical tests were rerun on today's source.

## Goal

- Close the historical four-test lifetime report against the surviving repair
  and the deliberate removal of the superseded handoff architecture.

## Non-goals

- Changing production asset-import shutdown order or `JobService::SnapshotAll()`
  ownership in this documentation-only closure.
- Quarantining, excluding, or disabling the affected tests.
- Rewriting the unrelated RUNTIME-198 history that exposed the original bug.

## Context

- Historical symptom (2026-07-27): the required serial `ci-asan` CPU gate
  passed 2,774 of 2,778 selected tests but reported one
  `stack-use-after-scope` and three `heap-use-after-free` failures:
  - `AssetWorkflowModule.BlockedImportIsSafeInBothOrdinaryOwnerShutdownOrders`
    read the loop-local `releaseWorker` atomic from the queued test hook after
    that iteration's stack state had expired.
  - `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs`
  - `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstQueuesObjectSpaceNormalBakeWhenInputsReady`
  - `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstDoesNotCpuFallbackWhenNormalBakeBackendIsNonOperational`
    retained `FindJob(...)` pointers into temporary vectors returned by
    `JobService::SnapshotAll()` and dereferenced them after the vectors were
    destroyed.
- Expected behavior: test callbacks remain valid until their worker is joined,
  and snapshot observations retain their owning vector for every pointer
  dereference.
- Historical impact: the required ASan gate was red on `origin/main`, obscuring
  sanitizer evidence for unrelated work while the ordinary full CPU gate passed.
- Provenance: the offending callback capture (`478b0d7d0`) and temporary
  snapshot pointer pattern (`15beaef54`) predated the exposing RUNTIME-198
  change, which did not touch either test.
- Reproduction observed 2026-07-27:

  ```text
  99% tests passed, 4 tests failed out of 2778
  SUMMARY: AddressSanitizer: stack-use-after-scope ... Test.AssetWorkflowModule.cpp:1961
  SUMMARY: AddressSanitizer: heap-use-after-free ... Test.AssetModelSceneHandoff.cpp
  ```

## Supersession evidence

- RUNTIME-200 repaired the surviving shutdown-order regression. The
  [current test](../../tests/contract/runtime/Test.AssetWorkflowModule.cpp)
  waits for `workerFinished` before either iteration releases its loop-local
  synchronization state.
- Commit `0fdeac008` deleted `Test.AssetModelSceneHandoff.cpp` together with
  the superseded handoff surface during RUNTIME-200's staged-import migration.
  All three snapshot-pointer regressions named above are absent from the
  current test tree; they were not repaired or recreated under BUG-122.
- RUNTIME-200 records a cache-disabled clean rebuild and revised-surface
  verification. Its [serial ASan receipt](../evidence/RUNTIME-200/commands/review-v2-asan-full-cpu-retry.json)
  records exit code zero on 2026-07-31 for the unchanged full CPU selector;
  its retirement record reports 2,667/2,667 selected tests. This is historical
  implementation evidence, not verification rerun for this closure.
- The original demand to pass all four exact tests is superseded by this
  repair/removal mapping. It does not justify restoring deleted architecture
  or weakening present coverage. A new failure needs a current reproduction;
  unrelated open asset-workflow bugs remain open.

## Required changes

- [x] Map the surviving callback-lifetime regression to the existing repair.
- [x] Map all three snapshot-lifetime regressions to the intentional handoff
      retirement and identify the deleting commit.
- [x] Preserve production code and current tests in this bookkeeping closure.

## Tests

- [x] Inspect the surviving lifetime guard and verify removal of the three
      retired regressions against source and Git history.
- [x] Inspect the superseding task's historical sanitizer receipt; do not
      represent it as a fresh test run.
- [x] Run the structural checks listed below for this task-only closure.

## Docs

- [x] Record the failure and supersession evidence in this task and the
      retirement log; remove the open bug entry and regenerate the brief.

## Acceptance criteria

- [x] Each original failure has an explicit existing-repair or retired-surface
      disposition, without claiming all four old tests still exist or pass.
- [x] Current source and semantic assertions remain unchanged by this closure.
- [x] No sanitizer selector, label, timeout, or quarantine policy is weakened.
- [x] Task structure, lifecycle links, documentation links, and brief freshness
      pass for the retiring surface.

## Verification

Current documentation-only closure checks (historical sanitizer evidence is
linked above):

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes

- Restoring retired tests merely to satisfy the historical selector.
- Weakening lifetime diagnostics, test selection, or semantic assertions.
- Mixing production refactors into a test-harness repair without a minimized
  production failure.

---
id: BUG-122
theme: none
depends_on: []
---
# BUG-122 — Runtime asset ASan tests retain expired callback and snapshot state

## Goal
- Make the four runtime asset contracts below lifetime-safe under the required
  serial `ci-asan` CPU gate without weakening their shutdown, progressive-job,
  or diagnostic assertions.

## Non-goals
- Changing production asset-import shutdown order or `JobService::SnapshotAll()`
  ownership unless an independently minimized production repro requires it.
- Quarantining, excluding, or disabling the affected tests.
- Folding this fix into the unrelated `RUNTIME-198` visualization-recipe
  cleanup that exposed it.

## Context
- Symptom: the required serial `ci-asan` CPU gate passes 2,774 of 2,778
  selected tests but reports one `stack-use-after-scope` and three
  `heap-use-after-free` failures:
  - `AssetWorkflowModule.BlockedImportIsSafeInBothOrdinaryOwnerShutdownOrders`
    reads the loop-local `releaseWorker` atomic from the queued test hook after
    that iteration's stack state has expired.
  - `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs`
  - `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstQueuesObjectSpaceNormalBakeWhenInputsReady`
  - `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstDoesNotCpuFallbackWhenNormalBakeBackendIsNonOperational`
    retain `FindJob(...)` pointers into temporary vectors returned by
    `JobService::SnapshotAll()` and dereference them after the vectors are
    destroyed.
- Expected behavior: test callbacks remain valid until their worker is joined,
  and snapshot observations retain their owning vector for every pointer
  dereference.
- Impact: the required ASan gate is red on `origin/main`, obscuring sanitizer
  evidence for unrelated work even though the ordinary full CPU gate passes.
- Provenance: the offending callback capture (`478b0d7d0`) and temporary
  snapshot pointer pattern (`15beaef54`) are both ancestors of
  `origin/main`; `RUNTIME-198` does not touch either test.
- Reproduction observed 2026-07-27:

  ```text
  99% tests passed, 4 tests failed out of 2778
  SUMMARY: AddressSanitizer: stack-use-after-scope ... Test.AssetWorkflowModule.cpp:1961
  SUMMARY: AddressSanitizer: heap-use-after-free ... Test.AssetModelSceneHandoff.cpp
  ```

## Required changes
- [ ] Keep the blocked-import hook's synchronization state alive through worker
      completion in both shutdown orders, and prove the hook is withdrawn
      before that state can expire.
- [ ] Store every `SnapshotAll()` result whose element address is retained,
      then search and inspect within that owning snapshot's lifetime.
- [ ] Audit the two affected test files for the same temporary-vector pointer
      pattern and repair all occurrences in the same bounded test-only slice.
- [ ] Preserve the production shutdown and `SnapshotAll()` contracts unless a
      separate minimized production failure proves a production change is
      necessary.

## Tests
- [ ] All four exact regressions pass under `ci-asan`.
- [ ] The complete serial `ci-asan` CPU selector passes.
- [ ] The ordinary focused runtime asset contracts remain green.

## Docs
- [ ] Record the exact failure mechanism and sanitizer evidence in this task
      and the retirement log; no architecture-doc change is required for a
      test-only lifetime repair.

## Acceptance criteria
- [ ] No affected callback or returned element pointer outlives its owning
      state under ASan.
- [ ] Shutdown-order and progressive-job assertions remain semantically
      unchanged.
- [ ] No sanitizer selector, label, timeout, or quarantine policy is weakened.

## Verification
```bash
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure \
  -R 'AssetWorkflowModule\.BlockedImportIsSafeInBothOrdinaryOwnerShutdownOrders|RuntimeAssetModelSceneHandoff\.ProgressiveRawGeometryFirst(PublishesNormalsAndQueuesUvAndBakeJobs|QueuesObjectSpaceNormalBakeWhenInputsReady|DoesNotCpuFallbackWhenNormalBakeBackendIsNonOperational)' \
  --timeout 60 --parallel 1
ctest --test-dir build/ci-asan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error \
  --timeout 60 --parallel 1
```

## Forbidden changes
- Shipping a fix without the exact ASan regressions.
- Weakening lifetime diagnostics, test selection, or semantic assertions.
- Mixing production refactors into a test-harness repair without a minimized
  production failure.

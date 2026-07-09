---
id: BUG-063
theme: G
depends_on: []
---
# BUG-063 — Streaming-import contract tests flaky on main

## Goal
- Make `RuntimeAssetImportFormatCoverage.DroppedModelSceneAndTextureImportThroughStreamingQueue`
  and `...ManualModelSceneAndTextureImportQueueCompletesThroughStreaming`
  deterministic under slow shared-runner conditions, restoring a green
  default CPU gate on `main`.

## Non-goals
- No quarantine without diagnosis first — these guard the streaming import
  path end-to-end; quarantining removes real coverage.
- No changes to the streaming executor's production behavior without a
  reproduced root cause.

## Context
- Symptom: `ci-linux-clang` on `main` is red since at least 2026-07-07
  14:24 (`a51e0511`, a markdown-only merge) with
  `ManualModelSceneAndTextureImportQueueCompletesThroughStreaming (Failed)`;
  on PR #1010 head `e732e69` (2026-07-08 ~14:27) both streaming-queue
  tests failed in `ci-linux-clang` and `pr-fast` while all other ~3,567
  tests passed. Markdown-only diffs cannot affect this path — the failures
  are independent of the diffs under test.
- Expected behavior: both tests complete their streaming decode within the
  test's 256-frame ready-predicate budget
  (`WaitForConditionApplication`, 1 ms sleep per variable tick) on any
  runner.
- Impact: `main` and every PR intermittently red on the default CPU gate;
  reviewers learn to ignore red gates, which is how real regressions slip.
- Root cause: three independently registered CTest cases in
  `Test.AssetImportFormatCoverage.cpp` created the same external glTF buffer
  path, `assetio004_triangle.bin`. Under parallel CTest execution,
  `RepresentativePromotedFormatsMaterializeDeterministically` could finish
  first and remove that shared file while either streaming test was still
  decoding it. Both imports then correctly reached a terminal
  `DecodeFailed`/`AssetDecodeFailed` state; the 256-frame budget and production
  streaming executor were not implicated.
- Deterministic local reproduction: the two streaming tests passed 100/100
  when run alone, while adding the representative-format test with `-j 3`
  reproduced both CI failures during a 25x until-fail run. The CI log showed
  the same three-test overlap and the same model-scene decode failure while
  each texture import succeeded.
- 2026-07-08 ~15:00 evidence (PR #1010 head `5d0c773`, a docs-only
  commit): only the Manual variant failed while Dropped passed —
  classic flake variance — and an unrelated timing-sensitive test,
  `CoreTasks.StaleWaitTokenUnparkDoesNotResumeNewWaiters` (unit;core),
  failed in the same round on the same docs-only diff. The scheduler
  wait/wake hardening owner for that area is `CORE-007`; if the
  CoreTasks flake recurs, file it as its own bug against that line
  rather than widening this task.

## Required changes
- [x] Reproduce with per-test output (`--output-on-failure` artifact or
      rerun-repeat locally) and identify which assertion trips (ready
      predicate exhausted vs. terminal-status mismatch).
- [x] Give each glTF fixture a distinct external-buffer URI/path, keeping the
      end-to-end streaming coverage and production behavior unchanged.

## Tests
- [ ] The three formerly colliding tests pass together under `-j 3` for 25
      repetitions.
- [ ] The full `RuntimeAssetImportFormatCoverage` contract group and default
      CPU-supported gate pass.

## Docs
- [x] Record the diagnosis and fix in this task; update `bugs/index.md`.

## Acceptance criteria
- [ ] Parallel CTest execution cannot delete another format-coverage test's
      external glTF buffer.
- [ ] `ci-linux-clang` completes with no streaming-import failures.

## Verification
```bash
ctest --test-dir build/ci -j 3 \
  -R 'RuntimeAssetImportFormatCoverage\.(RepresentativePromotedFormatsMaterializeDeterministically|DroppedModelSceneAndTextureImportThroughStreamingQueue|ManualModelSceneAndTextureImportQueueCompletesThroughStreaming)$' \
  --output-on-failure --repeat until-fail:25 --timeout 60
ctest --test-dir build/ci -R RuntimeAssetImportFormatCoverage \
  --output-on-failure --repeat until-fail:25 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Quarantining the tests without a recorded diagnosis and follow-up owner.

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
- Likely axis: shared-runner slowness (the same 2026-07-08 window showed
  ~+50 % configure-time variance, see `BUG-062`); a bounded frame budget
  plus real-time decode on loaded runners is timing-sensitive.

## Required changes
- [ ] Reproduce with per-test output (`--output-on-failure` artifact or
      rerun-repeat locally) and identify which assertion trips (ready
      predicate exhausted vs. terminal-status mismatch).
- [ ] Fix deterministically (e.g. condition-driven completion wait instead
      of fixed frame budget, or budget scaled for CI), keeping the
      end-to-end streaming coverage.

## Tests
- [ ] Repeat-gate evidence: the two tests pass a repeat run (e.g. 25x)
      under the sanitizer-enabled `ci` preset on a CI runner.

## Docs
- [ ] Record the diagnosis and fix in this task; update `bugs/index.md`.

## Acceptance criteria
- [ ] `ci-linux-clang` green on `main` across three consecutive runs with
      no streaming-import failures.

## Verification
```bash
ctest --test-dir build/ci -R RuntimeAssetImportFormatCoverage --output-on-failure --repeat until-fail:25
```

## Forbidden changes
- Quarantining the tests without a recorded diagnosis and follow-up owner.

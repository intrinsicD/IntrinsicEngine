---
id: BUG-117
theme: G
depends_on: []
---
# BUG-117 — Dropped-geometry reimport test exhausts a frame-count wait budget

## Goal
- Make the dropped-geometry reimport contract wait for asynchronous import
  completion with a deterministic, wall-clock-bounded test seam that remains
  reliable under ordinary host contention.

## Non-goals
- No asset import, reimport, entity materialization, or selection behavior
  change without evidence of a production defect.
- No retry-until-green CTest policy, flaky quarantine, label exclusion, or
  blanket timeout increase.
- No arbitrary increase to the current `128`-frame ceiling and no production
  sleep added to the Engine frame loop.
- No absorption into `RUNTIME-188`; the observed failure is outside scene-
  interaction ownership and its focused cohort is clean.

## Context
- Owner: runtime contract-test asynchronous completion harness around
  `WaitForAssetImportEventApplication` in
  `tests/contract/runtime/Test.SandboxEditorSceneCommands.cpp`.
- Symptom: on 2026-07-19, after a successful `IntrinsicTests` build, the
  canonical sequential CPU selector failed only
  `SandboxEditorUi.DroppedGeometryAssetReimportReloadsSameAssetWithoutDuplicateEntity`.
  At line 2643, `GetLastAssetImportEvent()` was still empty after
  `Engine::Run()` exhausted the helper's `128` frame limit; shutdown then
  reported the still-pending import as cancelled.
- Immediate evidence: the exact case passed in isolation in `0.05 s`.
  Without a source or build change, a second complete canonical CPU run passed
  all `4,197` selected cases in `50.23 s`; the one
  `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` capability skip was
  expected and unrelated. The original full run completed in `52.59 s` with
  `4,196` passes, this one failure, and the same expected skip.
- The helper counts tight Null/headless frames rather than elapsed time and
  does not yield when the event is absent. A fast main thread can therefore
  consume all `128` frames before the streaming worker publishes completion.
  This is the leading hypothesis, not yet a proven production scheduling bug.
- Falsifiable alternatives are: the completion is accepted but its event is
  published after the frame-loop observation point; an import dependency
  remains transiently unbound on one boot ordering; or the worker is delayed
  by host scheduling/filesystem contention independently of main-thread
  starvation. Instrument request, worker completion, main-thread apply, event
  publication, and exit reason before selecting a correction.
- `BUG-092` and `BUG-113` provide precedent for replacing fixed iteration/frame
  assumptions with an explicit steady-clock budget and observable completion.
  Reuse their shape only if instrumentation confirms the same cause.
- `RUNTIME-184` will remove the production `IApplication` callback lifecycle
  and must migrate this fixture if it lands first, but neither task is an
  artificial prerequisite for the other.

## Required changes
- [ ] Add test-local diagnostics that distinguish event observed, frame budget
      exhausted, worker completion pending, main-thread apply pending, and
      terminal cancellation; preserve the import operation identifier in
      failure output.
- [ ] Reproduce under controlled CPU/filesystem contention and record elapsed
      time plus frame count from request through worker completion, apply, and
      event publication.
- [ ] Confirm or reject main-thread frame-loop starvation as the cause before
      changing the wait contract.
- [ ] If confirmed, replace the fixed-frame-only helper with the smallest
      steady-clock-bounded completion wait that yields between unsuccessful
      polls and fails with the instrumented state. Keep the existing Engine
      path and real streaming worker; do not mock away the asynchronous seam.
- [ ] If a production lifecycle or publication defect is instead proven,
      scope that correction separately and preserve the deterministic harness
      evidence here.

## Tests
- [ ] Add a regression that drives the exact dropped import → event → reimport
      → same-asset/no-duplicate-entity path under forced scheduling contention.
- [ ] Prove the old frame-count helper can exhaust before a valid completion,
      or record sufficient repeated evidence to reject that hypothesis.
- [ ] Run the repaired exact case at least `100` consecutive times without a
      retry wrapper, then run its neighboring dropped-file/import cohort.
- [ ] Run the complete default CPU-supported selector and preserve the exact
      payload-ticket generation and one-entity assertions.

## Docs
- [ ] Update this task with the diagnosed cause and measured budget evidence.
- [ ] Update the bug index and retirement log when the correction is verified;
      update test-harness documentation only if the supported waiting policy
      changes.

## Acceptance criteria
- [ ] The original missing-event failure is deterministically reproduced or
      rejected by bounded evidence that identifies a different cause.
- [ ] The exact test no longer depends on how many zero-work frames the main
      thread can execute before the worker is scheduled.
- [ ] A genuinely stuck import fails within a declared wall-clock budget and
      reports its operation phase rather than only `droppedEvent == nullopt`.
- [ ] Reimport still reuses the same `AssetId`, advances the payload ticket,
      and creates no duplicate mesh entity.
- [ ] No RUNTIME-188 source, lifecycle, or timing workaround is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
build/ci/bin/IntrinsicRuntimeContractTests \
  --gtest_filter='SandboxEditorUi.DroppedGeometryAssetReimportReloadsSameAssetWithoutDuplicateEntity' \
  --gtest_repeat=100 --gtest_break_on_failure
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditorUi\.(DroppedGeometryAssetReimport|DuplicateDroppedGeometryImport|DroppedFileQueue|PlatformDrop)' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Masking the failure with retries, quarantine, exclusions, warning-only
  handling, or an unexplained frame/timeout increase.
- Adding sleeps or polling state to production runtime solely for this test.
- Treating one passing isolated run or one passing full rerun as proof that
  the defect is fixed.
- Weakening the same-asset, newer-generation, or no-duplicate-entity
  assertions.

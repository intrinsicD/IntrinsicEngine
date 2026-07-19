---
id: BUG-117
theme: G
depends_on: []
---
# BUG-117 — Dropped-geometry reimport test exhausts a frame-count wait budget

## Status
- Completed on 2026-07-19 at maturity `CPUContracted`; owner: Codex; branch:
  `bug-117-dropped-reimport-wait-aggregate`; retirement commit: this commit.
- The correction is confined to the two test-local completion helpers and
  their diagnostics/regressions; production import/runtime behavior is
  unchanged.
- No broader test-harness policy changed, so no architecture or agent-process
  documentation update is required.

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
- Owner: the two runtime test-local asynchronous completion helpers named
  `WaitForAssetImportEventApplication` in
  `tests/contract/runtime/Test.SandboxEditorSceneCommands.cpp` and
  `tests/integration/runtime/Test.SandboxEditorPresentation.cpp`.
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
- A later focused CPU rerun after `GRAPHICS-127` independently failed
  `SandboxEditorUi.DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade`
  at its copied `128`-frame helper with an empty event and shutdown
  cancellation; that exact integration case also passed unchanged in
  isolation in `0.05 s`. This sibling evidence is owned here because the
  helper shape and failure are identical; no graphics source participates.
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
- Controlled diagnosis used the existing real queued-geometry
  before-decode hook. With the old helper intact, the exact reimport contract
  exhausted frame `128` after `30,511 us`; operation `0:1` was still
  `Decoding`, its worker had started at `1,202 us`, the gate had not released,
  no apply/event timestamp existed, and shutdown then cancelled the request.
  This deterministically proves that a frame-count ceiling is not a valid
  completion budget for the real asynchronous path.
- The controlled gate does not by itself prove that ordinary CPU starvation
  was the unique mechanism behind either historical flake. The two loaded-run
  failures, immediate isolated passes, and cancellation-only teardown are
  consistent with host scheduling contention; the precise historical worker
  phase was not retained. The narrower confirmed cause is the harness
  contract: tight Null frames can expire independently of valid worker
  latency. No lost event, dependency failure, or production lifecycle defect
  was reproduced.
- The selected correction is test-only. Both helper copies use a ten-second
  `steady_clock` deadline that begins on the first completion poll and sleep
  one millisecond after each unsuccessful poll. The primary helper reports
  operation, frames, elapsed time, queue stage, worker/apply/event timestamps,
  timeout, and cancellation state. A stuck-worker regression first allows a
  separately bounded ten-second worker-start handshake, then proves a
  `250 ms` blocked-decode deadline and terminal cancellation.
- The exact reimport regression deliberately withholds decode until after
  frame `128`, then completes through the real worker, main-thread apply, and
  event publication. It preserved same-asset identity, one mesh entity, and
  payload-ticket generation advancement in `100/100` runs (`19.87 s` total).
  The ambiguous-PLY sibling passed `100/100` (`2.48 s` total), and the combined
  contract/integration drop/import cohort passed `8/8`.
- With the fixed test process and a competing busy loop pinned to the same
  single CPU, the exact real-worker regression passed `20/20` in `5.21 s`.
  The complete `IntrinsicTests` aggregate then built successfully and the
  default CPU-supported selector passed `4,180/4,180` in `55.32 s`; the one
  GLFW/LSan capability skip was expected and unrelated.

## Required changes
- [x] Add test-local diagnostics that distinguish event observed, frame budget
      exhausted, worker completion pending, main-thread apply pending, and
      terminal cancellation; preserve the import operation identifier in
      failure output.
- [x] Reproduce under controlled CPU/filesystem contention and record elapsed
      time plus frame count from request through worker completion, apply, and
      event publication.
- [x] Confirm or reject main-thread frame-loop starvation as the cause before
      changing the wait contract.
- [x] If confirmed, replace the fixed-frame-only helper with the smallest
      steady-clock-bounded completion wait that yields between unsuccessful
      polls and fails with the instrumented state. Keep the existing Engine
      path and real streaming worker; do not mock away the asynchronous seam.
- [x] If a production lifecycle or publication defect is instead proven,
      scope that correction separately and preserve the deterministic harness
      evidence here.

## Tests
- [x] Add a regression that drives the exact dropped import → event → reimport
      → same-asset/no-duplicate-entity path under forced scheduling contention.
- [x] Prove the old frame-count helper can exhaust before a valid completion,
      or record sufficient repeated evidence to reject that hypothesis.
- [x] Run the repaired exact case at least `100` consecutive times without a
      retry wrapper, then run its neighboring dropped-file/import cohort.
- [x] Run the complete default CPU-supported selector and preserve the exact
      payload-ticket generation and one-entity assertions.

## Docs
- [x] Update this task with the diagnosed cause and measured budget evidence.
- [x] Update the bug index and retirement log when the correction is verified;
      update test-harness documentation only if the supported waiting policy
      changes.

## Acceptance criteria
- [x] The original missing-event failure is deterministically reproduced or
      rejected by bounded evidence that identifies a different cause.
- [x] The exact test no longer depends on how many zero-work frames the main
      thread can execute before the worker is scheduled.
- [x] A genuinely stuck import fails within a declared wall-clock budget and
      reports its operation phase rather than only `droppedEvent == nullopt`.
- [x] Reimport still reuses the same `AssetId`, advances the payload ticket,
      and creates no duplicate mesh entity.
- [x] No RUNTIME-188 source, lifecycle, or timing workaround is introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target \
  IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
build/ci/bin/IntrinsicRuntimeContractTests \
  --gtest_filter='SandboxEditorUi.DroppedGeometryAssetReimportReloadsSameAssetWithoutDuplicateEntity' \
  --gtest_repeat=100 --gtest_break_on_failure
build/ci/bin/IntrinsicSandboxEditorIntegrationTests \
  --gtest_filter='SandboxEditorUi.DroppedFilePathsRouteAmbiguousPlyThroughRuntimeImportFacade' \
  --gtest_repeat=100 --gtest_break_on_failure
ctest --test-dir build/ci --output-on-failure \
  -R 'SandboxEditorUi\.(DroppedGeometryAssetReimport|DuplicateDroppedGeometryImport|DroppedFileQueue|PlatformDrop|DroppedFilePathsRouteAmbiguousPly)' \
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

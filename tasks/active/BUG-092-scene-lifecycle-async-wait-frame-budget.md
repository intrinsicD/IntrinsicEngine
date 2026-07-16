---
id: BUG-092
theme: G
depends_on: []
---
# BUG-092 — Scene lifecycle async wait exhausts its frame budget under delayed I/O

## Status

- Completed on 2026-07-16 at maturity `CPUContracted`; owner: Codex; branch:
  `codex/arch-006-completion`.
- Root cause and controlled-delay reproduction are complete. The selected
  repair is test-only: a ten-second steady-clock deadline, a one-millisecond
  unsuccessful-poll yield, and explicit satisfied/timed-out state.

## Goal
- Make the queued scene-save/load lifecycle contracts deterministic under
  bounded worker and filesystem delays without weakening their end-to-end
  main-loop coverage.

## Non-goals
- No production `SceneDocument`, `StreamingExecutor`, scheduler, or engine
  maintenance-order change without evidence of a production defect.
- No retry-until-green behavior, quarantine, or unbounded wait.
- No blanket CTest timeout increase based on a single loaded-host observation.
- No conversion of the queued scene-file contracts into synchronous I/O tests.

## Context
- Owner: runtime contract-test harness in
  `tests/contract/runtime/Test.RuntimeSceneLifecycle.cpp`; production scene I/O
  remains owned by `Extrinsic.Runtime.SceneDocument` and the existing runtime
  streaming lane.
- Symptom: during the 2026-07-16 default CPU gate under severe host I/O
  pressure,
  `RuntimeSceneLifecycle.QueuedSceneSaveWritesSnapshotAndMarksHistoryOnCompletion`
  exhausted `WaitForConditionApplication` at exactly 256 frames, observed no
  `RuntimeSceneFileEvent`, and then logged a successful save while engine
  teardown drained the worker. The failed case took 3.78 s.
- Expected behavior: a bounded, valid queued save may complete slowly under
  ordinary host contention, and the contract should keep driving maintenance
  until its elapsed-time budget expires. An absent observed completion must
  stop the main loop with actionable helper state; the existing 30-second
  per-test CTest property remains the outer guard if a non-returning worker
  also prevents `ShutdownAndDrain()` from returning.
- Impact: unrelated runtime/architecture work can lose the required CPU gate
  even though scene serialization and shutdown draining behave correctly.
- Root cause: this file's `WaitForConditionApplication` bounds asynchronous
  work by 256 rapid frames but does not yield or reserve elapsed time for the
  worker. `Engine::RunFrame()` launches the save during end-of-frame
  maintenance; under a delayed write, the main thread can consume all 256
  frames and request exit before a later maintenance pass can drain and apply
  the valid completion. Comparable async runtime helpers in
  `Test.AssetImportFormatCoverage.cpp`, `Test.SandboxEditorMeshMethods.cpp`,
  and `Test.SandboxEditorSceneCommands.cpp` sleep for 1 ms between unsuccessful
  predicate checks specifically to avoid this starvation shape.
- Local evidence: the isolated CTest passed 100/100 repetitions in 21.97 s
  total (individual cases about 0.21–0.24 s), so a passing retry does not
  resolve the loaded-host defect. Tracing the real worker and delaying its
  first `write(3, ..., 624)` by five seconds reproduced the exact assertions:
  `Frames() == 256`, absent completion event, followed by the successful-save
  log from `ShutdownAndDrain()`; the test body failed after 5.14 s. A one-second
  injected delay still passed, ruling out a lost-completion or serialization
  deadlock.
- Regression provenance: `git blame` traces the frame-only helper to
  `4d2ab126` and the queued-save test to `87f66fbe`, both from 2026-07-06.
  `RUNTIME-178` changes neither this test, `Runtime.SceneDocument`, nor
  `Runtime.StreamingExecutor`; its `Engine::RunFrame()` diff preserves the
  maintenance order and only changes private helper ownership from values to
  pointers. The failure is therefore not caused by `RUNTIME-178`.
- Selected repair and evidence: the test-local helper now uses a ten-second
  `steady_clock` budget and sleeps one millisecond after each unsuccessful
  predicate check. A checked-in regression succeeds only on predicate call
  257 and requires at least 200 ms elapsed, so restoring either the old frame
  ceiling or the no-yield loop fails deterministically. A second regression
  exercises an explicit five-millisecond timeout. All five focused contracts
  passed 20 consecutive repetitions each (100 executions in 62.15 seconds),
  and the three real scene-file paths passed 100 consecutive repetitions each
  (300 executions in 66.65 seconds). Re-running the Linux `strace` injection
  at the real save seam delayed the 624-byte worker write by five seconds and
  the fixed test passed in 5.151 seconds; the same injection failed the old
  helper at 5.14 seconds. `SceneDocument` constructs its file backend inside
  the worker, so adding a production injection hook solely for this test was
  rejected as disproportionate; the checked-in helper regression plus the
  repeatable syscall-delay command split portable contract coverage from
  real-I/O operational evidence.
- The aggregate `IntrinsicTests` build and default CPU-supported gate passed;
  the latter completed 3,787/3,787 tests in 402.05 seconds. Independent review
  found no remaining determinism, lifetime, timeout-semantics, right-sizing, or
  documentation blocker.
- Ranked hypotheses and disposition:
  1. Frame-count-only waiting expires before a delayed worker completion:
     confirmed by the five-second syscall-delay repro.
  2. Scene serialization hangs or loses its completion: rejected because the
     delayed operation succeeds during the existing shutdown drain.
  3. A shared temp-file collision removes or corrupts the fixture: unsupported;
     the exact path has one repository test owner and the injected isolated run
     reproduces without a competing test.
  4. `RUNTIME-178` reordered streaming maintenance: rejected by the focused
     diff and provenance audit.

## Required changes
- [x] Replace the helper's frame-count-only stop condition with an explicit
      `std::chrono::steady_clock` deadline and a small bounded yield/sleep after
      each unsuccessful predicate check; retain a diagnostic frame count but
      make elapsed time the failure budget.
- [x] Expose whether the helper stopped because its predicate succeeded or its
      deadline expired so assertions report the actual asynchronous timeout
      rather than cascading from a missing optional event.
- [x] Apply the corrected helper contract to queued save, invalid queued load,
      and successful queued load without changing production I/O behavior.
- [x] Calibrate the deadline below the effective 30-second per-test CTest
      property using clean
      and controlled-delay evidence; normal successful runs must not pay the
      full deadline.

## Tests
- [x] Pin the old 256-frame failure in a portable checked-in helper regression
      and retain the repeatable five-second syscall-delay command as real
      runtime/scene-file-seam evidence without adding a production sleep or
      injection API.
- [x] Prove delayed helper success within the declared elapsed-time budget and
      direct timeout state when no completion is observed; retain the CTest
      process timeout as the outer guard for a non-drainable worker.
- [x] Run all three queued scene-file lifecycle contracts repeatedly in
      isolation and together; retain snapshot, event, history, and fail-closed
      load assertions.
- [x] Run the default CPU-supported gate after the focused repair.

## Docs
- [x] Record the selected elapsed-time budget, controlled-delay distribution,
      and final diagnosis in this task; update the bug index and retirement log
      when the repair is verified.
- [x] Keep the policy local to this one helper; no broader runtime/testing
      guidance is warranted by a one-file test-harness repair.

## Acceptance criteria
- [x] The real queued scene-save test remains end-to-end through
      `Engine::RunFrame()` maintenance and main-thread completion application.
- [x] A completion delayed within the declared budget cannot be mistaken for a
      missing event solely because the Null backend advanced 256 frames.
- [x] An absent observed completion bounds `Engine::Run()` and reports direct
      satisfied/timed-out state plus elapsed time and frame count; CTest's
      30-second property still bounds a worker that cannot drain.
- [x] No production runtime behavior, module surface, or layering edge changes
      unless new evidence demonstrates that the test-only correction is
      insufficient.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeSceneLifecycle\.(AsyncWaitAllowsCompletionBeyondLegacyFrameBudget|AsyncWaitReportsElapsedTimeout|QueuedSceneSaveWritesSnapshotAndMarksHistoryOnCompletion|QueuedSceneLoadInvalidDocumentFailsClosed|QueuedSceneLoadAppliesParsedSceneOnMainThread)$' \
  --repeat until-fail:20 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeSceneLifecycle\.(QueuedSceneSaveWritesSnapshotAndMarksHistoryOnCompletion|QueuedSceneLoadInvalidDocumentFailsClosed|QueuedSceneLoadAppliesParsedSceneOnMainThread)$' \
  --repeat until-fail:100 --timeout 60

# Real-seam controlled-delay evidence: the pre-fix test reaches 256 frames and
# fails before the worker write returns; the fixed test observes and applies
# the same five-second-delayed completion before its ten-second deadline.
cd build/ci/tests
LSAN_OPTIONS='suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0' \
  strace -f -qq -e trace=write \
    -e inject=write:delay_enter=5s:when=1 \
    ../bin/IntrinsicRuntimeContractTests \
    --gtest_filter=RuntimeSceneLifecycle.QueuedSceneSaveWritesSnapshotAndMarksHistoryOnCompletion

ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Increasing the 256-frame count and calling the defect fixed without an
  elapsed-time contract and controlled-delay evidence.
- Sleeping or blocking in production scene-file or engine frame-loop code to
  make the test pass.
- Treating one passing retry as proof of determinism.
- Weakening snapshot correctness, event correlation, command-history, or
  invalid-load fail-closed assertions.

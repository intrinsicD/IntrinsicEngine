---
id: BUG-092
theme: G
depends_on: []
---
# BUG-092 — Scene lifecycle async wait exhausts its frame budget under delayed I/O

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
  until its elapsed-time budget expires. A genuinely hung task must still fail
  closed with an actionable timeout assertion.
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
- [ ] Replace the helper's frame-count-only stop condition with an explicit
      `std::chrono::steady_clock` deadline and a small bounded yield/sleep after
      each unsuccessful predicate check; retain a diagnostic frame count but
      make elapsed time the failure budget.
- [ ] Expose whether the helper stopped because its predicate succeeded or its
      deadline expired so assertions report the actual asynchronous timeout
      rather than cascading from a missing optional event.
- [ ] Apply the corrected helper contract to queued save, invalid queued load,
      and successful queued load without changing production I/O behavior.
- [ ] Calibrate the deadline below the test's 60-second CTest limit using clean
      and controlled-delay evidence; normal successful runs must not pay the
      full deadline.

## Tests
- [ ] Turn the controlled delayed-worker scenario into repeatable regression
      evidence at the real runtime/scene-file seam, using the existing test
      helper or a narrowly scoped test hook rather than a production sleep.
- [ ] Prove the queued-save contract passes when completion arrives within the
      declared elapsed-time budget and fails with an explicit timeout when it
      does not.
- [ ] Run all three queued scene-file lifecycle contracts repeatedly in
      isolation and together; retain snapshot, event, history, and fail-closed
      load assertions.
- [ ] Run the default CPU-supported gate after the focused repair.

## Docs
- [ ] Record the selected elapsed-time budget, controlled-delay distribution,
      and final diagnosis in this task; update the bug index and retirement log
      when the repair is verified.
- [ ] Update runtime/testing guidance only if the fix establishes a reusable
      asynchronous contract-test wait policy.

## Acceptance criteria
- [ ] The real queued scene-save test remains end-to-end through
      `Engine::RunFrame()` maintenance and main-thread completion application.
- [ ] A completion delayed within the declared budget cannot be mistaken for a
      missing event solely because the Null backend advanced 256 frames.
- [ ] A genuinely absent completion remains bounded and reports a direct,
      actionable timeout reason.
- [ ] No production runtime behavior, module surface, or layering edge changes
      unless new evidence demonstrates that the test-only correction is
      insufficient.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeSceneLifecycle\.(QueuedSceneSaveWritesSnapshotAndMarksHistoryOnCompletion|QueuedSceneLoadInvalidDocumentFailsClosed|QueuedSceneLoadAppliesParsedSceneOnMainThread)$' \
  --repeat until-fail:100 --timeout 60

# Diagnostic reproduction of the original bug: the pre-fix test reaches 256
# frames before the worker's first scene-file write returns, then applies the
# successful completion only during shutdown. Keep this as diagnostic evidence
# unless a portable controlled-delay seam replaces it.
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

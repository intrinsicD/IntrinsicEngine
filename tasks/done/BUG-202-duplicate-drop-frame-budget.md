---
id: BUG-202
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive test-harness diagnosis; evidence is the diff and regression runs
contract_schema: 1
contracts: []
contract_review: test-local async completion waiting only; no catalog-owned production or workflow contract changes
---
# BUG-202 — Duplicate dropped import exhausts a fixed frame budget

## Goal

Wait for the original dropped import to finish independently of Null frame speed,
while retaining duplicate rejection and exactly-one materialization assertions.

## Context

Found during the operator-directed UI-037 duplication/compilation cleanup on
2026-09-17. The full canonical CPU gate selected 4,724 tests and failed only
`SandboxEditorUi.DuplicateDroppedGeometryImportUsesSingleIngestRecord`: no mesh,
record still Decoding, no Result; shutdown cancelled the pending request.
The exact unchanged case then passed 100 isolated repetitions. Logs live in
`/tmp/intrinsic-config-locality/test-cpu.log` and `drop-repeat.log` locally.
This test still uses FixedFrameApplication(128), unlike the related BUG-117
reimport test, which already uses completion and wall time.

Ranked hypotheses: (1) fixed frames expire before valid worker completion;
(2) duplicate handling loses the first completion; (3) temporary-file interference.
Hold decode with the existing real-worker barrier until after frame 128. The
first probe retains a 128-frame ceiling; the fixed version uses the existing
condition helper with a steady-clock deadline. A pending duplicate rejection
already populates GetLastAssetImportEvent, so event presence alone is not a valid
completion signal. Queue terminality retains failure visibility in final asserts.
Reuse WaitForConditionApplication and QueuedGeometryDecodeBarrier; no production
sleep, new wait abstraction, timeout/label weakening or quarantine.

## Acceptance criteria

- [x] Reproduce the missing completion with a controlled real decode worker.
- [x] Replace the frame ceiling with bounded observable completion; preserve all duplicate, entity and diagnostic assertions.
- [x] Pass 100 controlled repetitions, neighboring import tests and the full CPU gate.
- [x] Claude reviews the fixed diff; record results and retire this test-only fix.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi.DuplicateDroppedGeometryImportUsesSingleIngestRecord$' --repeat until-fail:100 --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi.(Dropped|DuplicateDropped|AssetWorkflowMaterializes)' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Diagnosis ledger

- Full CPU run: one pending-completion failure; isolated unchanged case passes 100/100.
- Controlled existing decode barrier, same 128-frame ceiling through the condition
  helper: deterministic failure at frame 128, mesh count 0, Decoding, no Result,
  shutdown cancellation (205 ms). `bug-controlled-before.log` retains output.
  This demonstrates a defective wait contract, not the exact historical worker
  scheduling delay. The barrier prevents completion irrespective of file speed.
- Fix: same predicate/barrier, steady-clock deadline, release at frame 129. All
  original duplicate rejection and one-record/one-mesh/result/event checks remain.
  The two existing condition-helper consumers inherit a wall-time budget too.

## Completion — 2026-09-17

Completed at CPUContracted; this is the intended endpoint for a test-only waiting
contract, with no backend implementation or deferred maturity work. Commit reference: the enclosing duplicate-import-wait fix commit.

The fixed controlled case passed 100/100 repetitions in 22.13 s, and all ten
neighboring import/enrichment tests passed in 1.27 s. The final IntrinsicTests
aggregate built; the default CPU selector passed 4,723 tests with one expected
GLFW/LeakSanitizer capability skip (4,724 selected; 146.28 s). The initial failure
is retained in the diagnosis ledger, not treated as a successful retry.

Claude approved the fixed diff with no hard blockers. Verified its two review
questions: the controlled runs finish in about 0.22 s each, far below the ten-second
deadline; SnapshotQueue counts the state-machine records, whose CompleteApply
stores Result before setting Complete, after geometry materialization and
main-thread completion handling. The duplicate rejection creates no second record.
No lost completion or fixture interference was reproduced. The controlled test
proves the narrower invalid-frame-budget cause; it does not identify the original
host scheduling delay. Existing final phase/result/event assertions retain failure
visibility, and the barrier releases on early assertion exits before engine teardown.

A completion-based waiter at the original call site would have prevented this
failure. Reused that existing owner instead of adding another application/helper.
No debug instrumentation or production change remains. No test labels, exclusions, retry-until-green wrappers or quarantine were added.

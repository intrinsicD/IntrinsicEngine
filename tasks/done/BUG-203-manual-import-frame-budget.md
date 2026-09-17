---
id: BUG-203
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive test-harness fix; diff and controlled regression runs retain evidence
contract_schema: 1
contracts: []
contract_review: test-local async completion waiting; no catalog-owned production or workflow contract changes
---
# BUG-203 — Manual geometry import exhausts a fixed frame budget

## Goal
Wait for queued manual import completion independently of frame speed and worker
scheduling while preserving responsiveness, exactly-one apply and cancellation checks.

## Context
Observed during the UI-037 full CPU gate: `SandboxEditorUi.QueuedManualGeometryImportsRemainResponsiveAndApplyOnce`
exits with PointCloud ActiveCount=1, TerminalCount=0 and no entity/history/selection
publication. The fixture `DriveBlockedGeometryImportApplication` exits after 512
frames regardless of decode completion. The preceding full gate passed unchanged.
Evidence: `/tmp/intrinsic-normal-config-locality/final-cpu.log`.

Reuse the adjacent `WaitForConditionApplication` steady-clock deadline pattern;
the blocked-import fixture retains its distinct release/cancel and ImGui progress
checks. No new wait abstraction, production change, label or CTest timeout change.
A controlled point-cloud decode remains blocked through 513 responsiveness frames,
so the original 512-frame ceiling fails independently of scheduling speed.

## Acceptance criteria
- [x] Reproduce the fixed-frame exit with the controlled decode worker.
- [x] Replace the total-frame ceiling with a ten-second steady-clock deadline; retain all command/result assertions.
- [x] Pass controlled repeated and neighboring import tests, plus the full CPU gate.
- [x] Review the fixed test-only diff with Claude and record the result.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests -j4
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi.QueuedManualGeometryImportsRemainResponsiveAndApplyOnce$' --repeat until-fail:25 --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi.(QueuedManual|CancelledQueued|DuplicateDropped)' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests -j4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

Controlled pre-fix run fails exactly at 512 blocked frames versus required 513,
with the original pending queue/no-publication assertions also failing. The
barrier is released on deadline/shutdown so failure cannot strand the worker.
Evidence: `bug203-before.log` in the same temporary evidence directory.


## Completion — 2026-09-17

Completed at CPUContracted, the intended endpoint for this test-only fix;
no production change or deferred work. Commit reference: the enclosing
manual-import-wait fix commit. The final controlled case passes 25/25 repetitions (20.93 s),
four neighboring import tests pass (1.35 s), and the final canonical ci/Clang 23
IntrinsicTests build plus full CPU gate pass: 4,731 passed, one expected ASan-only
GLFW lifecycle skip, zero failures (4,732 selected; 152.55 s).

Claude accepts the deadline and threading logic. Addressed its timeout-visibility
finding with an explicit DeadlineExpired flag asserted by both fixture consumers.
Retained a deadline from the first frame so a worker that never starts also fails
boundedly; observed controlled runs are about 0.82 s, below the ten-second budget.
The deterministic reproduction proves the invalid frame ceiling, not the original
host scheduling delay. All responsiveness, cancellation and exactly-one apply
assertions remain. No timeout/label relaxation, quarantine or production sleeps.
One over-broad test assertion edit caused a compile failure in the adjacent
shutdown fixture; removed that assertion before the final build and verification.
Cancelled intermediate runs and failed attempts remain in the temporary evidence
directory; only `complete-cpu.log` records the final combined gate.

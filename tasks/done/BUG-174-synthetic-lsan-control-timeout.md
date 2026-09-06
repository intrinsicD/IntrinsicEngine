---
id: BUG-174
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Incidental one-variable test-harness environment repair during METHOD-040 verification. No engine, suppression, assertion, or timeout changes; parent command receipts and BUG-174 discriminating probes retain the complete evidence."
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Investigates an existing sanitizer control subprocess; no current catalog contract governs its exit-time processing. Preserve the BUG-082/118 leak detection and exact suppression contracts."
---
# BUG-174 — Synthetic LSan control exceeds its subprocess time budget

## Goal
- Diagnose the exit-time stall in the 4096-byte synthetic engine-leak control
  and restore reliable execution without weakening leak detection.

## Non-goals
- No suppression expansion, timeout inflation without evidence, quarantine,
  removal of the negative control, or attribution to GLFW without reaching it.

## Context
- METHOD-040's 2026-09-06 full ASan gate reached
  `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`, printed
  `BUG082_SYNTHETIC_ENGINE_LEAK_ALLOCATED: bytes=4096`, then exceeded the inner
  ten-second subprocess budget before a LeakSanitizer report or exit 86.
  The parent receipt `tasks/evidence/METHOD-040/commands/asan-ctest-final.json`
  and its stdout retain the failure. UBSan compilation was running concurrently.
- Actual GLFW initialization/teardown had not run. This does not establish a
  recurrence of the former GLFW lifetime leak. Cold/contended symbolization,
  sanitizer exit-time processing, and another process teardown stall remain
  unproven hypotheses. A later pass alone does not diagnose the failure.

## Required changes
- [x] Reproduce with bounded fresh-process executions and capture the child/symbolizer
      process state during the stall; distinguish symbolization from leak scanning.
- [x] Fix the demonstrated cause while keeping the exact reviewed suppressions,
      explicit synthetic allocation, required LSan report, and exit 86 control.

## Tests
- [x] The synthetic leak is detected and the clean lifetime path remains clean.
- [x] Repeated fresh processes exercise the original inherited-environment failure conditions.

## Docs
- [x] Retain failing and passing logs and document the proven cause and host scope.

## Acceptance criteria
- [x] Before/after evidence establishes a cause and its repair.
- [x] The original leak-control assertions and CPU/sanitizer selection remain intact.

## Verification
```bash
cmake --preset ci-asan
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -R '^GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl$' --repeat until-fail:20 --timeout 60 --parallel 1
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding the ten-second failure, changing suppressions to silence the synthetic
  engine allocation, or describing the failure as a confirmed GLFW leak.

## Diagnosis — 2026-09-06
- The second full ASan run reproduced the same timeout with no concurrent build.
  An isolated helper waited in `pipe_read` on its llvm-symbolizer-18 child; the
  child waited in `poll` with negligible CPU use. The inherited
  `DEBUGINFOD_URLS=https://debuginfod.ubuntu.com` enabled remote debug lookup.
- Changing only `DEBUGINFOD_URLS` to empty yielded three runs with exit 86 in
  0.099/0.073/0.076 seconds, each reporting the named 4096-byte direct leak with
  symbolized source lines. The binary, llvm-symbolizer-18, suppression file,
  sanitizer options, and ten-second limit stayed identical.
- The harness now clears that variable for its two subprocesses. Twenty required
  harness repetitions pass, including the clean GLFW lifetime check without a
  capability skip; the independent source review found no issue.
- [Isolated evidence](../evidence/BUG-174/summary.md) retains the failing
  process states and both discriminating environment probes. No caches were
  cleared; this establishes fresh-process reliability, not cold-cache timing.
- Independent source review accepted the exact harness change at SHA-256
  `6e1e6ac9d24ad68f04d4f5bf060fc7fe1a56ec77a9cb05e55c61002447de404d`.
- Verification: `ctest --test-dir build/ci-asan --output-on-failure -R
  '^GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl$' --repeat until-fail:20
  --timeout 60 --parallel 1` passed all twenty runs in 2.81 seconds. The retained
  log is [harness-repeat.log](../evidence/BUG-174/harness-repeat.log).

## Completion
- Completed 2026-09-06.
- Commit: 2d22f6f01 (implementation).
- The remaining METHOD-040 full-gate rerun is separate from the completed
  bounded harness repair.
- The original investigation placeholder used the high-risk profile. Once the
  cause was isolated, the repair became one test-harness environment variable;
  it follows the same incidental micro lane as BUG-173/175, with all diagnostic
  evidence retained. No high-risk execution claim was acquired for this bug.

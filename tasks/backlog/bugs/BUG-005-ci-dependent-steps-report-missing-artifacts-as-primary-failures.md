# BUG-005 — CI dependent steps report missing artifacts as primary failures

## Goal
- Make CI/test-harness reporting distinguish root build failures from downstream steps that cannot run because required binaries or benchmark output directories were never produced.

## Non-goals
- No attempt to fix the underlying compile/dependency failures in this task.
- No reduction of required test coverage.
- No broad workflow redesign beyond explicit artifact/dependency guards.

## Context
- Status: backlog.
- Owner/agent: unassigned.
- Observed: 2026-05-09 while continuing the local CI sweep after earlier build/configure failures.
- Symptom: downstream checks fail with misleading missing-artifact errors:
  - `ctest --test-dir build/ci ...` reports 19 `*_NOT_BUILT` tests as failed/not run.
  - `build/ci/bin/IntrinsicCoreTests --gtest_filter=ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes` exits 127 because the binary is missing.
  - `python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark --strict` exits 2 because the benchmark output root does not exist.
- Expected behavior: local CI scripts and workflow steps should fail fast or report a skipped/blocked status when prerequisite build targets/artifacts are absent, preserving the root-cause failure.
- Impact: failure triage is noisy, and missing-artifact follow-on errors can obscure the first actionable build or dependency defect.

## Required changes
- Audit CI scripts/workflows and local verification helpers for steps that assume `IntrinsicTests`, `IntrinsicCoreTests`, `IntrinsicBenchmarkSmoke`, `IntrinsicBenchmarks`, or `build/ci/benchmark` already exist.
- Add explicit prerequisite checks before CTest, architecture SLO, and benchmark-result validation steps.
- Prefer messages that point back to the missing build target and prior log rather than registering placeholder `*_NOT_BUILT` tests as primary failures.
- Keep the default CPU-supported correctness gate unchanged when prerequisites are present.

## Tests
- Add script/tool tests for missing-artifact handling if the guards live in repository tools.
- Verify that a complete build still runs CTest, SLO, benchmark smoke, and benchmark-result validation normally.

## Docs
- Update CI/troubleshooting docs if local CI collection commands now have explicit prerequisite behavior.
- No architecture docs are expected.

## Acceptance criteria
- Running CTest before test binaries exist produces a clear blocked/prerequisite message or is not scheduled as a primary verification step.
- Architecture SLO and benchmark-validation steps check for their required binaries/directories before execution.
- CI reports retain the first actionable build/configure failure and do not inflate the failure list with expected missing-artifact consequences.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
build/ci/bin/IntrinsicCoreTests --gtest_filter=ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not hide real test failures once the corresponding binaries and artifacts exist.
- Do not remove the `NOT_BUILT` convention without replacing it with an equally visible prerequisite failure policy.
- Do not combine missing-artifact reporting fixes with dependency or benchmark behavior changes.

## Captured evidence
- `build/ci-full-logs/ctest_full_cpu.log` reports 19 `*_NOT_BUILT` tests as failed/not run.
- `build/ci-full-logs/slo_framegraph.log` reports `bash: line 1: build/ci/bin/IntrinsicCoreTests: No such file or directory`.
- `build/ci-full-logs/validate_benchmark_results.log` reports `ERROR: root path does not exist: build/ci/benchmark`.


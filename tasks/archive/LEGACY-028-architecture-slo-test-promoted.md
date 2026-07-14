---
id: LEGACY-028
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-028 — Migrate ArchitectureSLO test to promoted Core

## Goal
- [x] Move the architecture SLO benchmark test off the legacy aggregate
      `Core` import and onto promoted `Extrinsic.Core.FrameGraph` and
      `Extrinsic.Core.Tasks` APIs.

## Non-goals
- Do not change SLO thresholds, measured workloads, warmup counts, or measured
  frame counts.
- Do not add or modify benchmark manifests, benchmark IDs, runner JSON, or
  baselines.
- Do not make performance claims from this migration.
- Do not touch the legacy `Core.Benchmark` runner test in this slice.

## Context
- Owner/layer: benchmark/SLO test migration under the `LEGACY-012` legacy
  consumer-test cleanup program.
- `tests/benchmark/slo/Test_ArchitectureSLO.cpp` imported bare `Core` only to
  use core `FrameGraph`, `Memory::ScopeStack`, and `Tasks` APIs. The retained
  promoted equivalents are `Extrinsic.Core.FrameGraph`,
  `Extrinsic.Core.Tasks`, and `Extrinsic.Core.Tasks.CounterEvent`.
- The benchmark workflow does not require manifest or result-schema updates
  here because this slice only changes test imports and filename convention; it
  does not add a benchmark workload or alter thresholds.

## Plan
- Rename the test to `tests/benchmark/slo/Test.ArchitectureSLO.cpp`.
- Replace the legacy aggregate import with promoted Core module imports.
- Update the SLO test body for promoted `FrameGraph` timing accessors.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Migrate SLO test imports to promoted Core modules.
- [x] Rename the touched independent test to the current `Test.<Name>.cpp`
      convention.
- [x] Update `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the benchmark/SLO test target.
- [x] Run focused `ArchitectureSLO` CTest coverage.
- [x] Confirm the remaining Core test-consumer count is current.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.
- [x] Update current task records that named the old SLO filename.

## Acceptance criteria
- [x] `tests/benchmark/slo/Test.ArchitectureSLO.cpp` imports only promoted
      `Extrinsic.Core.*` modules.
- [x] SLO thresholds and measured workload sizes are unchanged.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarkTests
ctest --test-dir build/ci --output-on-failure -R 'ArchitectureSLO' -LE 'gpu|vulkan|flaky-quarantine' --timeout 120
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Changing the SLO budgets while migrating imports.
- Adding benchmark-result claims without baseline comparison.
- Hiding the test behind new labels.
- Mixing this migration with legacy `Core.Benchmark` runner cleanup.

## Maturity
- Target: `CPUContracted` via promoted Core SLO test compilation and execution
  under the existing slow benchmark label policy.
- No `Operational` follow-up is owed for this import migration.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 28 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicBenchmarkTests` — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'ArchitectureSLO' -LE 'gpu|vulkan|flaky-quarantine' --timeout 120`
  — passed 2/2 selected tests; both SLO bodies reported skipped under the
  sanitizer-enabled `ci` preset, matching their existing test guards.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 28 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` —
  passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.

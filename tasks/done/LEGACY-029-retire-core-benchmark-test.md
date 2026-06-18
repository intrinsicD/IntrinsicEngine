---
id: LEGACY-029
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-029 — Retire legacy Core.Benchmark test

## Goal
- [x] Remove the legacy `Core.Benchmark::BenchmarkRunner` unit test from the
      `LEGACY-005` external Core consumer set while preserving its promoted
      telemetry pass-timing assertions under `Extrinsic.Core.Telemetry`.

## Non-goals
- Do not promote `Core.Benchmark::BenchmarkRunner`.
- Do not change benchmark manifests, benchmark IDs, runner JSON schemas, or
      baselines.
- Do not alter SLO thresholds or benchmark smoke workloads.
- Do not delete `src/legacy/Core/` or legacy runtime benchmark-mode code.

## Context
- Owner/layer: benchmark/core test cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `docs/migration/nonlegacy-parity-matrix.md` records legacy
  `Core.Benchmark` as legacy-only cleanup owned by `LEGACY-012` or subtree
  deletion gates, not an unnamed promoted `Extrinsic.Core` feature blocker.
- `tests/benchmark/Test_Benchmark.cpp` also contained two pass-timing telemetry
  checks that map directly to promoted `Extrinsic.Core.Telemetry`; those checks
  move into `tests/unit/core/Test.CoreProfiling.cpp`.

## Plan
- Move `Telemetry_PassTimings` coverage to promoted
  `Test.CoreProfiling.cpp`.
- Delete `tests/benchmark/Test_Benchmark.cpp`.
- Simplify the benchmark/SLO test target now that only promoted Core SLO tests
  remain there.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Preserve pass-timing telemetry assertions under promoted Core.
- [x] Delete the legacy benchmark-runner unit test.
- [x] Update `tests/CMakeLists.txt` and `tests/benchmark/README.md`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected benchmark and promoted core wrapper test targets.
- [x] Run focused pass-timing telemetry and SLO CTest filters.
- [x] Confirm deleted legacy benchmark test cases are no longer discovered.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.
- [x] Update test/retired-task references to the retired benchmark-runner test.

## Acceptance criteria
- [x] `tests/benchmark/Test_Benchmark.cpp` no longer exists.
- [x] No test imports `Core.Benchmark`.
- [x] Promoted telemetry pass-timing coverage still runs under the default CPU
      unit label set.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarkTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'PassGpuTimings|PassCpuTimings|ArchitectureSLO' -LE 'gpu|vulkan|flaky-quarantine' --timeout 120
ctest --test-dir build/ci -N -R 'Benchmark_Runner|Telemetry_PassTimings'
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
- Reintroducing the legacy benchmark runner under promoted `core`.
- Changing benchmark thresholds, manifests, or baselines.
- Mixing this retirement with legacy runtime benchmark-mode cleanup.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy-only
  `Core.Benchmark` unit coverage plus promoted telemetry unit coverage.
- No `Operational` follow-up is owed for the retired legacy benchmark runner
  unit test.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 27 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicBenchmarkTests IntrinsicCoreWrapperUnitTests IntrinsicTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'PassGpuTimings|PassCpuTimings|ArchitectureSLO' -LE 'gpu|vulkan|flaky-quarantine' --timeout 120`
  — passed 4/4 selected tests; the two pass-timing telemetry tests passed and
  the two SLO bodies reported skipped under the sanitizer-enabled `ci` preset.
- `ctest --test-dir build/ci -N -R 'Benchmark_Runner|Telemetry_PassTimings'`
  — reported `Total Tests: 0` after rebuilding.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 27 test files.
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

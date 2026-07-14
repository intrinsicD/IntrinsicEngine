---
id: LEGACY-021
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-021 — Migrate profiling test to promoted Core

## Goal
- [x] Move `Test_Profiling.cpp` from legacy `Core.Telemetry`/`Core.Hash` imports
      to promoted `Extrinsic.Core.Telemetry`/`Extrinsic.Core.Hash`, use the new
      test filename convention, and reduce the `LEGACY-005` Core test-consumer
      set by one file.

## Non-goals
- Do not change `Extrinsic.Core.Telemetry` or `Extrinsic.Core.Hash` behavior.
- Do not delete `src/legacy/Core/`.
- Do not migrate unrelated Core tests in this slice.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/core/Test_Profiling.cpp` imports bare legacy `Core.Telemetry` and
  `Core.Hash` to exercise `ScopedTimer`, `TelemetrySystem`, `TimingCategory`,
  present timings, and hash-backed telemetry labels.
- The promoted replacements expose the same test-facing contracts through
  `Extrinsic.Core.Telemetry` and `Extrinsic.Core.Hash`.

## Plan
- Rename `Test_Profiling.cpp` to `Test.CoreProfiling.cpp`.
- Replace legacy imports/namespaces with promoted `Extrinsic.Core.*` modules.
- Move the test source from the legacy-linked core test list to the promoted
  core wrapper test list.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Rename and migrate the profiling/telemetry unit test to promoted Core.
- [x] Update `tests/CMakeLists.txt` source lists.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test targets.
- [x] Run the focused profiling/telemetry CTest filters.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] No `tests/unit/core/Test_Profiling.cpp` legacy consumer remains.
- [x] `Test.CoreProfiling.cpp` imports promoted `Extrinsic.Core.Telemetry` and
      `Extrinsic.Core.Hash`, not legacy `Core.*`.
- [x] The touched independent test uses the `Test.<Name>.cpp` naming
      convention.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'Profiling|TelemetryPresentTiming' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this profiling cleanup with broader Core test migrations.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `tests/unit/core/Test.CoreProfiling.cpp` now carries the profiling/telemetry
  coverage against promoted `Extrinsic.Core.Telemetry` and
  `Extrinsic.Core.Hash`. The broader `LEGACY-005` Core gate now records 133
  legacy-internal consumers and 37 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'Profiling|TelemetryPresentTiming' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 12/12 tests.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 37 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.

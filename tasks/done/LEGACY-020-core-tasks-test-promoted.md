---
id: LEGACY-020
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-020 — Migrate CoreTasks test to promoted Core

## Goal
- [x] Move the legacy `Test_CoreTasks.cpp` scheduler/counter-event coverage to
      promoted `Extrinsic.Core.Tasks`, use the new test filename convention,
      and reduce the `LEGACY-005` Core test-consumer set by one file.

## Non-goals
- Do not change `Extrinsic.Core.Tasks` or `Extrinsic.Core.Telemetry` behavior.
- Do not delete `src/legacy/Core/`.
- Do not migrate unrelated Core tests in this slice.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/core/Test_CoreTasks.cpp` imports bare legacy `Core` to exercise
  scheduler dispatch, coroutine jobs, `CounterEvent`, wait-token safety,
  telemetry export, and job move/destruction behavior.
- The promoted replacements are `Extrinsic.Core.Tasks`,
  `Extrinsic.Core.Tasks.CounterEvent`, and `Extrinsic.Core.Telemetry`.
- The promoted telemetry endpoint uses `TelemetrySystem::SetTaskStats(...)`
  with a `Telemetry::TaskSchedulerStats` payload and stores results under
  `FrameStats::Tasks`.
- `tests/unit/core/Test.Core.TasksLegacy.cpp` already carries smaller promoted
  wrapper coverage; this slice replaces it with the fuller migrated test and
  removes the legacy-suffixed filename.

## Plan
- Rename `Test_CoreTasks.cpp` to `Test.CoreTasks.cpp`.
- Import promoted `Extrinsic.Core.Tasks`,
  `Extrinsic.Core.Tasks.CounterEvent`, and `Extrinsic.Core.Telemetry`.
- Adapt only the telemetry export assertion to the promoted task-stats API.
- Remove the smaller duplicate promoted wrapper test.
- Update `tests/CMakeLists.txt`, migration docs, and task notes with the
  reduced Core test-consumer count.

## Required changes
- [x] Rename and migrate the full CoreTasks unit test to promoted Core.
- [x] Remove `Test.Core.TasksLegacy.cpp`.
- [x] Update `tests/CMakeLists.txt` source lists.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.
- [x] Update historical test-taxonomy notes for the promoted filename.

## Tests
- [x] Build the affected core test targets.
- [x] Run the focused `CoreTasks` CTest filter.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.
- [x] Update the HARDEN-041 taxonomy move note for the renamed promoted test.

## Acceptance criteria
- [x] No `tests/unit/core/Test_CoreTasks.cpp` legacy consumer remains.
- [x] `Test.CoreTasks.cpp` imports promoted `Extrinsic.Core.Tasks` modules, not
      legacy `Core`.
- [x] The touched independent test uses the `Test.<Name>.cpp` naming
      convention.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'CoreTasks' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Forbidden changes
- Mixing this CoreTasks cleanup with broader Core test migrations.
- Introducing unrelated feature work.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: pending local commit.
- `tests/unit/core/Test.CoreTasks.cpp` now carries the full scheduler,
  coroutine, `CounterEvent`, wait-token, telemetry-export, and job lifetime
  coverage against promoted `Extrinsic.Core.Tasks` modules. The telemetry
  export assertion uses promoted `TelemetrySystem::SetTaskStats(...)` and
  `FrameStats::Tasks`. The smaller legacy-suffixed promoted wrapper test is
  deleted as duplicate coverage. The broader `LEGACY-005` Core gate now records
  133 legacy-internal consumers and 38 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreTasks' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  — passed, 16/16 tests.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 38 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.

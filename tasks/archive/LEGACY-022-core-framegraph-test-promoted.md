---
id: LEGACY-022
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-022 — Migrate CoreFrameGraph test to promoted Core

## Goal
- [x] Move the legacy Core frame-graph unit coverage and type-token helper from
      bare `Core` imports to promoted `Extrinsic.Core.*` modules, use the new
      test naming convention, and reduce the `LEGACY-005` Core test-consumer set
      by two files.

## Non-goals
- Do not change promoted `Extrinsic.Core.FrameGraph`, scheduler, hash, or tasks
  behavior.
- Do not migrate unrelated Core tests.
- Do not delete `src/legacy/Core/`.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/core/Test_CoreFrameGraph.cpp` and its helper translation unit
  imported the legacy aggregate `Core` module for frame graph, task scheduler,
  hash labels, and type tokens.
- The promoted equivalents are `Extrinsic.Core.FrameGraph`,
  `Extrinsic.Core.Hash`, and `Extrinsic.Core.Tasks`. The promoted frame graph
  owns its graph storage internally, so tests no longer pass a legacy
  `Memory::ScopeStack` backing store.

## Plan
- Rename the frame-graph test, helper source, and helper header to
  `Test.<Name>` style names.
- Replace legacy aggregate imports with explicit promoted Core module imports.
- Adapt small API differences: default `FrameGraph` construction,
  `PassCount()`/`PassName()`, and checked `FrameGraph::Execute()` results.
- Move the sources from the legacy-linked core test list to the promoted core
  wrapper test list.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Rename and migrate the frame-graph unit test and type-token helper.
- [x] Update `tests/CMakeLists.txt` source lists.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected core test targets.
- [x] Run the focused CoreFrameGraph CTest filter.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] No frame-graph unit test/helper source imports bare legacy `Core`.
- [x] Migrated frame-graph sources import promoted `Extrinsic.Core.*` modules.
- [x] The touched independent test sources use the `Test.<Name>` naming
      convention.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'CoreFrameGraph' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
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
- Mixing this frame-graph cleanup with broader Core test migrations.
- Introducing runtime, ECS, graphics, or scheduler feature changes.
- Editing legacy source.

## Maturity
- Target: `CPUContracted` for this focused test-consumer cleanup.
- No `Operational` follow-up is owed; this is a dependency-edge cleanup slice
  under `LEGACY-012`.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- `tests/unit/core/Test.CoreFrameGraph.cpp` and
  `tests/unit/core/Test.CoreFrameGraphTypeTokenHelper.cpp` now carry the
  frame-graph and cross-translation-unit type-token coverage against promoted
  Core modules. The broader `LEGACY-005` Core gate now records 133
  legacy-internal consumers and 35 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreFrameGraph' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed, 22/22 tests after rebuilding both affected executables.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 35 test files.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/validate_tasks.py --root tasks --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `python3 tools/agents/check_task_state_links.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/generate_session_brief.py --check` — passed.
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  — passed.
- `git diff --check` — passed.

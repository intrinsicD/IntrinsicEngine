---
id: LEGACY-026
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-026 — Retire legacy Core.DAGScheduler test

## Goal
- [x] Remove the legacy `Core.DAGScheduler` unit test from the `LEGACY-005`
      external Core consumer set because promoted DAG scheduler and graph
      compiler coverage already owns the retained scheduling contract.

## Non-goals
- Do not add a compatibility wrapper for the legacy `Core::DAGScheduler` API.
- Do not change promoted DAG scheduler semantics in this slice.
- Do not migrate runtime, graphics, RHI, ECS, or asset tests.
- Do not delete `src/legacy/Core/`.

## Context
- Owner/layer: `tests/unit/core` cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/core/Test_DAGScheduler.cpp` exercised the old aggregate
  `import Core;` API: node insertion, explicit edges, RAW/WAR/WAW/RAR resource
  ordering, weak reads, duplicate-edge behavior, reset/rebuild, and fan-out
  stress.
- The retained promoted contract lives in `Extrinsic.Core.Dag.Scheduler` and
  `Extrinsic.Core.Dag.TaskGraph`. Existing promoted tests cover empty/single
  graphs, explicit dependency layering, missing and duplicate task IDs, cycle
  diagnostics, RAW/WAR/WAW/RAR hazards, weak reads, duplicate resource
  accesses, deterministic repeated compiles, scheduler reset, HLFET priority
  ordering, and large hazard stress.

## Plan
- Remove `tests/unit/core/Test_DAGScheduler.cpp`.
- Remove the deleted source from the legacy-linked core test object list.
- Run focused promoted DAG scheduler/graph compiler coverage to prove the
  retained contract remains covered.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Delete the legacy `DAGScheduler` unit test.
- [x] Update `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected legacy and promoted core test targets.
- [x] Run promoted DAG scheduler and graph compiler CTest filters.
- [x] Confirm the deleted legacy `DAGScheduler` tests are no longer discovered
      after the rebuild.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/core/Test_DAGScheduler.cpp` no longer exists.
- [x] Promoted DAG scheduler and graph compiler tests remain in the default CPU
      test graph.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'CoreDagScheduler|CoreGraphCompiler|DAGScheduler' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R '^DAGScheduler\.'
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
- Reintroducing the legacy `Core::DAGScheduler` API under promoted `core`.
- Editing promoted scheduler implementation as part of this cleanup.
- Mixing this retirement with unrelated legacy consumer migrations.

## Maturity
- Target: `CPUContracted` via explicit retirement of legacy compatibility
  `Core.DAGScheduler` unit coverage.
- No `Operational` follow-up is owed for the retired old API test; retained DAG
  scheduling behavior is covered through promoted `Extrinsic.Core.Dag.*`
  contracts.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 30 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreDagScheduler|CoreGraphCompiler|DAGScheduler' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 33/33 focused promoted DAG scheduler and graph compiler tests.
- `ctest --test-dir build/ci -N -R '^DAGScheduler\.'` — reported
  `Total Tests: 0` after rebuilding.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 30 test files.
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

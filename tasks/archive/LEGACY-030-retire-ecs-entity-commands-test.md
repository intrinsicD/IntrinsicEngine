---
id: LEGACY-030
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-030 — Retire legacy ECS entity-command test

## Goal
- [x] Remove the duplicate legacy `Core`/`ECS` entity-command compatibility
      test from the `LEGACY-005` and `LEGACY-006` external consumer sets.

## Non-goals
- Do not change promoted ECS scene, hierarchy, or runtime command-history
  behavior.
- Do not promote the legacy `Core::CommandHistory` or `Core::EditorCommand`
  APIs.
- Do not delete `src/legacy/ECS/` or `src/legacy/Core/`.

## Context
- Owner/layer: ECS/runtime test cleanup under the `LEGACY-012` legacy
  consumer-test migration program.
- `tests/unit/ecs/Test_EntityCommands.cpp` imported bare `Core` and `ECS` only
  to exercise legacy editor create/delete command lambdas.
- Promoted coverage already exists:
  `tests/contract/runtime/Test.EditorCommandHistory.cpp` covers undo/redo,
  command failure rollback, transform/selection command adapters, and hierarchy
  delete planning through `Extrinsic.Runtime.EditorCommandHistory`; promoted ECS
  scene/bootstrap/hierarchy tests cover typed lifecycle and hierarchy mutation.

## Plan
- Delete the duplicate legacy entity-command compatibility test.
- Remove it from the ECS test object list.
- Update legacy-removal docs and backlog notes with the reduced Core/ECS test
  consumer counts.

## Required changes
- [x] Delete `tests/unit/ecs/Test_EntityCommands.cpp`.
- [x] Remove `Test_EntityCommands.cpp` from `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced consumer counts.

## Tests
- [x] Build the affected ECS and runtime contract test targets.
- [x] Run focused promoted `EditorCommandHistory`/ECS hierarchy coverage.
- [x] Confirm the retired legacy entity-command test is no longer discovered.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005`, `LEGACY-006`, and `LEGACY-012` blocker notes.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/ecs/Test_EntityCommands.cpp` no longer exists.
- [x] No test imports legacy `Core`/`ECS` only for the old entity-command
      compatibility lambdas.
- [x] Promoted command-history and ECS hierarchy/lifecycle tests remain wired
      and focused verification passes.
- [x] Remaining `LEGACY-005` and `LEGACY-006` test-consumer counts are current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|ECS\\.Hierarchy|ECS\\.SceneRegistry|ECS\\.SceneBootstrap' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
ctest --test-dir build/ci -N -R '^EntityCommands\\.'
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
- Reintroducing legacy command-history APIs under promoted `core` or `ecs`.
- Changing runtime/editor command semantics.
- Mixing this cleanup with legacy subtree deletion.

## Maturity
- Target: `CPUContracted` via explicit retirement of duplicate compatibility
  coverage and existing promoted command-history/ECS coverage.
- No `Operational` follow-up is owed for the retired legacy test.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 26 test consumers; the `LEGACY-006` ECS gate records 37 legacy-internal
  consumers and 24 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicECSTests IntrinsicRuntimeContractTests`
  — passed.
- Initial focused CTest run hit the known transient CMake 4.3 GoogleTest JSON
  discovery parse failure before test execution; immediate retry regenerated
  discovery cleanly.
- `ctest --test-dir build/ci --output-on-failure -R 'EditorCommandHistory|ECS\.Hierarchy|ECS\.SceneRegistry|ECS\.SceneBootstrap' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 6/6 selected `EditorCommandHistory` tests after discovery retry.
- `ctest --test-dir build/ci --output-on-failure -R 'SceneRegistry|SceneBootstrap|Hierarchy' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 127/127 selected scene/bootstrap/hierarchy tests.
- `ctest --test-dir build/ci -N -R '^EntityCommands\.'` — reported
  `Total Tests: 0`.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 26 test files.
- `git grep -nE '^\s*(export\s+)?import\s+ECS(\.|\b)|^\s*(export\s+)?import\s+ECS\b' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 24 test files.
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

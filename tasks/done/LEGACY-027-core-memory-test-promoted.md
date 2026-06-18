---
id: LEGACY-027
theme: F
depends_on: [LEGACY-012]
---
# LEGACY-027 — Migrate CoreMemory test to promoted Core

## Goal
- [x] Move the remaining legacy `Core.Memory` unit coverage to promoted
      `Extrinsic.Core.Memory` APIs and remove the old legacy-linked
      `Test_CoreMemory.cpp` consumer from the `LEGACY-005` external Core set.

## Non-goals
- Do not change promoted memory allocator behavior in this slice.
- Do not add compatibility aliases for legacy `Core::Memory` names.
- Do not migrate runtime, graphics, RHI, ECS, or asset tests.
- Do not delete `src/legacy/Core/`.

## Context
- Owner/layer: `tests/unit/core` migration under the `LEGACY-012` legacy
  consumer-test cleanup program.
- The legacy unit file tested `ScopeStack`, `LinearArena`, `ArenaAllocator`,
  PMR integration, thread-affinity checks, reset/reuse behavior, alignment, and
  destructor semantics through `import Core;`.
- The promoted surface is `Extrinsic.Core.Memory` with `LinearArena`,
  `ScopeStack`, `ArenaAllocator`, `ArenaMemoryResource`, and allocation
  telemetry in `Extrinsic.Core.Telemetry`.
- A smaller promoted parity file already existed as
  `Test.Core.MemoryLegacy.cpp`; this slice folds the retained legacy coverage
  into a single promoted `Test.CoreMemory.cpp` file using the current test
  naming convention.

## Plan
- Replace `Test_CoreMemory.cpp` and `Test.Core.MemoryLegacy.cpp` with promoted
  `tests/unit/core/Test.CoreMemory.cpp`.
- Update `tests/CMakeLists.txt` so the test is built by the promoted core
  wrapper target, not the legacy-linked core target.
- Run focused `CoreMemory` CTest coverage and confirm the old
  `Test_CoreMemory.cpp` import no longer contributes to the Core gate.
- Update migration docs and task notes with the reduced Core test-consumer
  count.

## Required changes
- [x] Migrate retained Core memory tests to promoted `Extrinsic.Core.Memory`.
- [x] Rename the surviving test file to the current `Test.<Name>.cpp`
      convention.
- [x] Update `tests/CMakeLists.txt`.
- [x] Update migration docs/task notes with the reduced Core test-consumer
      count.

## Tests
- [x] Build the affected legacy and promoted core test targets.
- [x] Run focused `CoreMemory` CTest coverage.
- [x] Confirm the remaining Core test-consumer count is current.

## Docs
- [x] Update `docs/migration/legacy-removal-audit.md`.
- [x] Update `LEGACY-005` and `LEGACY-012` blocker counts.
- [x] Update architecture backlog retired-task notes and retirement log.

## Acceptance criteria
- [x] `tests/unit/core/Test_CoreMemory.cpp` no longer exists.
- [x] Surviving memory coverage imports only promoted `Extrinsic.Core.*`
      modules.
- [x] Remaining `LEGACY-005` Core test-consumer count is current.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R 'CoreMemory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90
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
- Reintroducing legacy `Core::Memory` names in promoted tests.
- Changing allocator implementation semantics while migrating the test.
- Mixing this migration with unrelated legacy consumer cleanup.

## Maturity
- Target: `CPUContracted` via promoted CPU memory allocator unit coverage.
- No `Operational` follow-up is owed for this test migration.

## Status
- Completed 2026-06-18 at maturity `CPUContracted`.
- PR/commit: local commit in this slice.
- The broader `LEGACY-005` Core gate now records 133 legacy-internal consumers
  and 29 test consumers.

## Verification results
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicCoreWrapperUnitTests`
  — passed.
- `ctest --test-dir build/ci --output-on-failure -R 'CoreMemory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 90`
  — passed 27/27 focused promoted memory tests.
- `git grep -nE '^\s*(export\s+)?import\s+Core(\.|\b)|#include\s+"Core' -- 'tests/**' | cut -d: -f1 | sort -u | wc -l`
  — reports 29 test files.
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

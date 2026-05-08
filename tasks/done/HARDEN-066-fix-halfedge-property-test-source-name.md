# HARDEN-066 — Fix halfedge property test source name

## Goal
Fix the CI CMake regeneration blocker caused by a stale geometry test source filename.

## Non-goals
- No geometry behavior changes.
- No test assertion changes.
- No source file rename.

## Context
`tests/CMakeLists.txt` referenced `Test_HalfedgePropertyAccess.cpp`, but the repository contains `tests/unit/geometry/Test_HalfedgeMeshPropertyAccess.cpp`. During `cmake --build --preset ci --target ExtrinsicSandbox`, CMake regeneration failed while resolving geometry test sources.

## Required changes
- Update the geometry test source list in `tests/CMakeLists.txt` to use the existing `Test_HalfedgeMeshPropertyAccess.cpp` filename.

## Tests
- Re-run the sandbox build command that exposed the configure blocker.
- Run test layout and task policy checks.

## Docs
- No docs changes required beyond this task record.

## Acceptance criteria
- [x] CMake can resolve the halfedge mesh property access test source name.
- [x] The sandbox build no longer fails at this missing-source configure step.

## Verification
```bash
cmake --build --preset ci --target ExtrinsicSandbox
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No geometry implementation changes.
- No broad CMake/test taxonomy refactor.

## Execution log
- 2026-05-08: Replaced stale `Test_HalfedgePropertyAccess.cpp` entry with `Test_HalfedgeMeshPropertyAccess.cpp` in `tests/CMakeLists.txt`.
- 2026-05-08: `cmake --build --preset ci --target ExtrinsicSandbox` completed successfully after CMake regeneration resolved the corrected test source list.

## Completion metadata
- Completion date: 2026-05-08.
- Commit reference: pending current workspace/PR.
- Follow-up: None.


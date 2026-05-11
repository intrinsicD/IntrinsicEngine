# HARDEN-054 — Rename Extrinsic-only test sources to dot naming

## Goal
- Mechanically rename old `Test_*.cpp` test source files that are already wired only through `Extrinsic*` test module targets to the current `Test.*.cpp` naming convention.

## Non-goals
- Do not rename tests in CMake groups that still directly link `Intrinsic*` modules.
- Do not change test behavior, labels, directories, or production code.
- Do not split or relabel runtime/graphics GPU coverage.

## Context
- `tests/README.md` documents `Test.<Name>.cpp` for new C++ tests and treats `Test_*.cpp` names as compatibility carryover for explicit mechanical cleanup tasks.
- `tests/CMakeLists.txt` contains both legacy `Intrinsic*`-linked test groups and post-HARDEN-041 `Extrinsic*`-linked wrapper-derived groups.
- This task targets old-name files from Extrinsic-only object libraries: assets, core wrapper, RHI unit, graphics contract, and runtime integration.
- Source-level import audit also found one unlisted old-name ECS source that imports only `Extrinsic.ECS.*`; it is included as a mechanical filename cleanup without changing CMake membership.

## Required changes
- [x] Rename qualifying source files from `Test_<Name>.cpp` to `Test.<Name>.cpp` within their existing taxonomy directories.
- [x] Update `tests/CMakeLists.txt` source lists for the renamed files.
- [x] Leave Intrinsic-linked old-name tests untouched.
- [x] Synchronize test naming documentation if it still describes the old convention as canonical.

## Tests
- [x] Run the strict test layout checker.
- [x] Configure/build the relevant test targets or the repository aggregate when practical.
- [x] Run the default CPU-supported CTest gate when practical.

## Docs
- [x] Update test strategy naming policy if needed.
- [x] Archive this task with verification evidence when complete.

## Acceptance criteria
- [x] Only Extrinsic-only `Test_*.cpp` sources are renamed.
- [x] `tests/CMakeLists.txt` points at the new filenames.
- [x] Intrinsic-linked `Test_*.cpp` sources remain untouched.
- [x] Relevant validation/build/test commands have been run or blockers documented.

## Verification
```bash
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
cmake --preset ci -DINTRINSIC_HEADLESS_NO_GLFW=ON -DINTRINSIC_OFFLINE_DEPS=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=OFF -UFETCHCONTENT_SOURCE_DIR_DRACO -Udraco_SOURCE_DIR -Udraco_BINARY_DIR -Udraco_POPULATED
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Evidence captured on 2026-05-01:

- `python3 tools/repo/check_test_layout.py --root . --strict` passed with `findings=0`.
- `python3 tools/agents/check_task_policy.py --root . --strict` passed before archival with `findings=0`.
- `python3 tools/docs/check_doc_links.py --root . --strict` passed with no broken relative links.
- Source/CMake audit confirmed no remaining old-style `Test_*.cpp` source imports only `Extrinsic*`; remaining old-name CMake entries are in `Intrinsic*` or mixed dependency groups.
- Headless CI configure generated `build/ci` successfully with `INTRINSIC_HEADLESS_NO_GLFW=ON`; the plain full-GLFW configure path initially re-entered external dependency population and was not used as final evidence.
- `cmake --build --preset ci --target IntrinsicTests` passed and compiled the renamed dot-style test sources.
- Default CPU-supported CTest completed; `build/ci/Testing/Temporary/LastTestsFailed.log` was empty and `LastTest.log` ended cleanly.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Renaming tests that are still wired through `Intrinsic*` module dependencies.

## Completion metadata
- Completion date: 2026-05-01.
- Commit reference: pending current workspace/PR.
- Follow-up: none for this mechanical rename; any future full-GLFW dependency-cache cleanup should be tracked separately from test naming.


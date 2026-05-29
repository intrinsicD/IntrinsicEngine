# BUILD-003 — Select highest available Clang 20+ toolchain

## Goal
- Keep Clang 20 as the minimum supported C++23 module toolchain while making repository presets select the highest complete installed Clang toolchain automatically.

## Non-goals
- No engine source, module surface, shader, or runtime behavior changes.
- No compiler support below Clang 20.
- No CI package-manager policy changes beyond preserving compatibility with the existing minimum toolchain.

## Context
- Status: done.
- Completed: 2026-05-29.
- Commit references: this local commit / follow-up PR.
- Owner/layer: build system (`CMakePresets.json`, root `CMakeLists.txt`, and `cmake/` helper scripts); no engine layer dependency changes.
- A complete supported toolchain means matching `clang`, `clang++`, and `clang-scan-deps` binaries at major version 20 or newer.
- The previous preset hard-pinned `clang-20`/`clang++-20`, so hosts with only newer complete Clang toolchains had to override the preset manually.

## Required changes
- [x] Add CMake toolchain discovery that selects the highest complete installed Clang 20+ triplet before language enablement.
- [x] Update the base configure preset to use the discovery toolchain instead of hard-coded compiler binary names.
- [x] Keep root configure validation fail-closed for non-Clang or Clang older than 20.
- [x] Keep `clang-scan-deps` discovery and validation compatible with C++23 module scanning.

## Tests
- [x] Run CMake configure with the `ci` preset on this host and confirm the selected compiler/scanner in configure output.
- [x] Run a focused build-system/layering/doc validation subset for the touched files.

## Docs
- [x] Update the authoritative agent contract and skill mirror for the new minimum/highest-available policy.
- [x] Update build troubleshooting docs to document preset-driven toolchain selection.

## Acceptance criteria
- [x] `cmake --preset ci` no longer requires a `clang-20` binary when a higher complete Clang 20+ toolchain is installed.
- [x] Explicit compiler overrides remain possible but must satisfy the Clang 20+ validation guard.
- [x] The selected scanner is version 20 or newer, with a warning when it does not match the compiler major version.
- [x] No engine layer dependencies or module surfaces change.

## Verification
```bash
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicTests
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Verification results
- [x] `cmake --preset ci --fresh` — passed; selected `/bin/clang-23`, `/bin/clang++-23`, and `/bin/clang-scan-deps-23` with minimum `20.0`.
- [x] `cmake --build --preset ci --target IntrinsicTests` — passed; initial command exceeded the API timeout after reaching the final link steps, then an incremental rerun returned successfully.
- [x] `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — completed with 2332/2334 passed; the two failures were the pre-existing benchmark smoke entries `IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run` and `.Validate` reported as `Not Run`.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- [x] `python3 tools/docs/check_doc_links.py --root .` — passed.
- [x] `python3 tools/repo/check_layering.py --root src --strict` — passed.

## Forbidden changes
- Do not introduce GCC as a valid verification toolchain for module changes.
- Do not mix unrelated build, graphics, shader, or runtime fixes into this task.
- Do not edit user-modified rendering task records while documenting this build-system slice.



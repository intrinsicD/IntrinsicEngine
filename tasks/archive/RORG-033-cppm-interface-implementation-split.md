# RORG-033 — Split module interface implementations

## Status

- Completed: 2026-05-13.
- Commit: pending (lands with this task).
- PR: pending.

## Goal
- Move non-required implementation bodies out of non-legacy `src/**/*.cppm` module interfaces and into associated `.cpp` module implementation units.

## Non-goals
- No semantic behavior changes.
- No mechanical directory moves.
- No dependency boundary changes.
- No migration of `src/legacy` files.

## Context
- Owned by promoted `src/` engine layers.
- C++23 module interfaces should expose declarations and only retain bodies that must remain visible, such as templates, `constexpr` helpers, or intentionally inline trivial/performance-critical code.

## Required changes
- [x] Audit non-legacy `src/**/*.cppm` files for implementation bodies.
- [x] Move substantial non-template/non-constexpr implementations into associated `.cpp` files.
- [x] Register new `.cpp` files in the owning layer `CMakeLists.txt` files.

## Tests
- [x] Configure/build the focused target with the repository `ci` preset where practical.
- [x] Run the default CPU-supported CTest gate where practical.

## Docs
- [x] No architecture or public behavior docs required unless module surfaces or dependencies change.

## Acceptance criteria
- [x] Module interfaces touched by the cleanup contain declarations plus only necessary inline/template/constexpr bodies.
- [x] New implementation units compile as part of their owning targets.
- [x] Layering invariants remain unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Executed with local toolchain overrides because `clang-20`/`clang++-20` were not present on PATH while `clang-22`/`clang++-22` and `clang-scan-deps-22` were available:

```bash
/home/alex/.local/bin/cmake --preset ci -DCMAKE_C_COMPILER=/usr/bin/clang-22 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22 -DTINYGLTF_BUILD_LOADER_EXAMPLE=OFF -DTINYGLTF_HEADER_ONLY=ON -DTINYGLTF_INSTALL=OFF
/home/alex/.local/bin/cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing runtime behavior while moving implementations.

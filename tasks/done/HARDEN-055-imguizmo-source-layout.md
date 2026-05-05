# HARDEN-055 — Resolve ImGuizmo source layout during configure

## Goal
- Keep CMake configure working when the cached ImGuizmo checkout stores `ImGuizmo.cpp` under `src/` instead of the checkout root.

## Non-goals
- Do not vendor or copy third-party ImGuizmo sources.
- Do not change the centralized FetchContent/cache policy.
- Do not change graphics or runtime behavior.

## Context
- Symptom: CLion/CMake configure failed in `cmake/Dependencies.cmake` because it expected `external/cache/imguizmo-src/ImGuizmo.cpp` while the populated checkout contains `external/cache/imguizmo-src/src/ImGuizmo.cpp`.
- Expected behavior: configure should accept both flat and nested ImGuizmo source layouts.
- Impact: GLFW/Vulkan-enabled build trees could not reload, and IDE compiler information failed as follow-on noise after configure stopped.

## Required changes
- Resolve ImGuizmo from the documented `INTRINSIC_IMGUIZMO_SOURCE_DIR` override, the FetchContent source directory, or the shared `external/cache` fallback.
- Accept both `${IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp` and `${IMGUIZMO_SOURCE_DIR}/src/ImGuizmo.cpp`.
- Use the resolved source/include directory when defining `imguizmo_lib`.

## Tests
- No C++ test is required; this is configure-time dependency resolution.
- Verify with a fresh CMake configure and focused dependency target build.

## Docs
- This task record documents the build/dependency behavior change.

## Acceptance criteria
- CMake configure succeeds with the existing populated ImGuizmo cache.
- `imguizmo_lib` builds from the resolved source path.
- No layering or runtime behavior changes are introduced.

## Verification
```bash
cmake --preset ci --fresh \
  -DCMAKE_C_COMPILER=/usr/bin/clang-22 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-22 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22
cmake --build --preset ci --target imguizmo_lib -j2
```

## Forbidden changes
- Do not add symlinks or copied third-party files to paper over the layout mismatch.
- Do not require GPU/Vulkan test execution for this configure-time dependency fix.

## Completion metadata
- Completion date: 2026-05-05.
- Commit reference: pending current workspace/PR.

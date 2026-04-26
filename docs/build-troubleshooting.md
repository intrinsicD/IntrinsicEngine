# Build Troubleshooting (Container / CI)

This document explains the most common reason `src/Core`, `src/Geometry`, `src/ECS`, and test targets fail to configure/compile in minimal container environments.

## Symptom

`cmake --preset dev` fails during **Generate** with errors like:

- `... has C++ sources that may use modules, but the compiler does not provide a way to discover the import graph dependencies ...`

Affected targets typically include `IntrinsicCore`, `IntrinsicGeometry`, `IntrinsicECS`, and test object targets.

## Root cause

Intrinsic uses C++23 modules and relies on CMake's module dependency scanning.

When CMake auto-selects **GCC** (`/usr/bin/c++`), module scanning is unavailable/incomplete for this project configuration, so generation fails before compilation starts.

In contrast, `clang++-20` with `clang-scan-deps-20` supports the required dependency scanning flow used by this build.

## Verify quickly

```bash
cmake --version
c++ --version
clang++-20 --version
cmake --preset dev
```

If configure output shows `The CXX compiler identification is GNU ...` and then module import-graph errors, you are hitting this issue.

## Working options

### Option A (recommended): configure with Clang 20 explicitly

```bash
CC=clang-20 CXX=clang++-20 cmake -S . -B build/dev-clang -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINTRINSIC_BUILD_SANDBOX=ON \
  -DINTRINSIC_BUILD_TESTS=ON \
  -DINTRINSIC_ENABLE_SANITIZERS=ON \
  -DINTRINSIC_ENABLE_CUDA=OFF

cmake --build build/dev-clang --target IntrinsicCore IntrinsicGeometry IntrinsicECS
cmake --build build/dev-clang --target IntrinsicCoreTests IntrinsicECSTests IntrinsicGeometryTests
ctest --test-dir build/dev-clang --output-on-failure
```

### Option B: keep presets, but force compiler once per shell

```bash
export CC=clang-20
export CXX=clang++-20
cmake --preset dev
cmake --build --preset dev --target IntrinsicCore IntrinsicGeometry IntrinsicECS
ctest --test-dir build/dev --output-on-failure
```

### Option C: use a toolchain file / preset inheritance for team consistency

Create a preset/toolchain that pins Clang 20 so developer machines and CI select the same compiler deterministically.

## Additional container caveat (non-blocking for libs/tests)

Some containers do not provide X11 RandR headers; CMake enables `INTRINSIC_HEADLESS_NO_GLFW=ON` in that case. This disables GLFW-dependent runtime/sandbox modules but still allows core libraries and headless tests to build.

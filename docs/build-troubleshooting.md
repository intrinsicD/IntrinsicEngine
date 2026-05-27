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

## Dependency cache recovery

The repository centralizes FetchContent checkouts under `external/cache/`. CMake
serializes dependency population with per-dependency locks and validates required
marker files before reusing an existing `<name>-src` directory. A hot cache is
treated as sealed by default, so normal configures run without network access.

If an interrupted configure leaves a partial checkout, the validator fails closed
with a deterministic recovery message instead of deleting dependency sources.
Repopulate missing entries online with `tools/setup/populate_deps.sh --refresh`,
or manually remove the affected `<name>-src`, `<name>-build`, and
`<name>-subbuild` directories before refreshing.

Safe manual recovery steps:

```bash
rm -rf external/cache/glm-src external/cache/glm-build external/cache/glm-subbuild
rm -rf external/cache/eigen-src external/cache/eigen-build external/cache/eigen-subbuild
rm -rf external/cache/json-src external/cache/json-build external/cache/json-subbuild
rm -rf external/cache/volk-src external/cache/volk-build external/cache/volk-subbuild
tools/setup/populate_deps.sh --refresh
```

For offline workflows, repopulate the missing dependency directories first, then
configure with `INTRINSIC_OFFLINE_DEPS=ON`.

## Blocked follow-on CI steps

Some local CI sweeps intentionally continue after a build failure to collect logs.
CTest, SLO, and benchmark-result validation are dependent steps: they require the
producer targets and output directories to exist. Workflows use
`tools/ci/check_prerequisites.py` to report these as `BLOCKED` prerequisite
failures instead of presenting missing binaries or missing benchmark directories
as the root cause.

## Fast touched-scope verification

For local iteration on small, well-scoped changes, use the touched-scope helper
to plan or run the strongest relevant subset without waiting for the full CPU
gate every time:

```bash
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --run
```

The helper maps changed paths to conservative CMake targets, CTest labels, and
repository structural checks. For example, `src/geometry/` changes select
`IntrinsicGeometryTests`, `-L geometry`, and the layering check; docs/tasks-only
changes select docs/task validators without a C++ build. Build-system changes,
foundational `src/core/` changes, and unknown source paths fall back to the broad
CPU-supported gate.

This helper is an iteration aid, not a replacement for the canonical PR/merge
verification from `AGENTS.md`:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```


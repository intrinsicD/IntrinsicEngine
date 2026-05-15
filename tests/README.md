# Tests

Tests are organized by taxonomy-owned roots:

- `unit/` — isolated subsystem behavior.
- `contract/` — API, layering, lifecycle, and interoperability contracts.
- `integration/` — controlled cross-subsystem behavior.
- `regression/` — fixed reproducers for previously observed failures.
- `gpu/` — opt-in GPU/Vulkan coverage.
- `benchmark/` — benchmark smoke/SLO checks.
- `support/` — shared test-only fixtures and helpers.

## CTest labels

CTest labels are assigned per executable in `tests/CMakeLists.txt` and should
describe both the test category and the subsystem or capability being exercised.
The documented label set is:

- categories: `unit`, `contract`, `integration`, `regression`, `benchmark`,
  `slo`.
- subsystems/ownership: `assets`, `build`, `core`, `ecs`, `geometry`,
  `graphics`, `headless`, `platform`, `runtime`.
- capabilities/backends: `glfw`, `gpu`, `vulkan`.
- opt-in filters: `slow`, `flaky-quarantine`.

`tests/CMakeLists.txt` enforces this set at configure time through the test
executable helper. Additions must update this README and the helper's allow-list
in the same change before a new CTest label can be used.

Use `slow` for valid tests that should not run in fast PR or default local CPU
correctness gates, including executables that boot the full headless engine,
initialize Vulkan in a non-`gpu`-only path, run benchmark/SLO thresholds, or
regularly exceed one second of wall-clock time on the reference Linux-clang
runner. Do not add `slow` to pure CPU unit or contract suites merely because
they contain many cheap cases.

Use `flaky-quarantine` only as a temporary quarantine. Any test or executable
with this label must have a linked task ID, a reason, and a removal condition in
the relevant source or task record.

Common gates:

```bash
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

Opt-in promoted Vulkan smoke coverage is selected with the label intersection
form below. `IntrinsicGraphicsVulkanSmokeTests` includes
`MinimalDebugSurfaceGpuSmoke.ReferenceTriangleRecordsOnOperationalPromotedVulkan`,
the `GRAPHICS-033D` fixture that drives the reference triangle through the
bootstrap `FrameRecipe::MinimalDebug` path. It reports `SKIPPED` when GLFW or a
Vulkan-capable swapchain/device is unavailable and is intentionally excluded
from the default CPU gate by its `gpu;vulkan` labels.

```bash
ctest --test-dir build/ci --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
```

For fast local iteration on changed paths, use the touched-scope helper to plan
or run the relevant subset before the full gate:

```bash
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --run
```

The helper is conservative and may broaden to the full CPU-supported gate for
foundational or unknown changes; it is not a replacement for final PR/merge
verification.

`CMakePresets.json` currently defines configure/build presets but no CTest
`testPresets`, so use the directory-based `ctest --test-dir build/ci ...`
commands above rather than `ctest --preset ci`. Test registration sets a
30-second CTest `TIMEOUT` by default; executables or grouped entries labeled
`slow` receive a 120-second CTest `TIMEOUT` so opt-in/nightly coverage has room
for valid longer-running cases without raising the default for cheap CPU tests.

## Shared fixtures

Shared support fixtures live under `support/`. Runtime/Vulkan integration tests
that borrow `RuntimeRhiTestEnvironment` must keep per-test mutable state local
to each fixture and follow the reset contract in `support/README.md`.

Some slow runtime suites also have grouped CTest entries, such as
`IntrinsicRuntimeTests.RuntimeRHIGrouped`,
`IntrinsicRuntimeTests.RenderOrchestratorHeadlessGrouped`,
`IntrinsicRuntimeTests.HeadlessEngineGrouped`, and
`IntrinsicRuntimeTests.RenderGraphPacketsGrouped`. Smaller Vulkan-backed
fixtures also have grouped entries, including
`IntrinsicRuntimeTests.GraphicsBackendHeadlessGrouped`,
`IntrinsicRuntimeTests.HeadlessAppSmokeGrouped`,
`IntrinsicRuntimeTests.SceneManagerGpuHooksGrouped`,
`IntrinsicRuntimeTests.GeometryReuseGrouped`,
`IntrinsicRuntimeTests.AssetPipelineHeadlessGrouped`, and
`IntrinsicRuntimeTests.MaintenanceLaneGpuGrouped`. These entries let shared
per-process fixtures amortize setup cost in nightly/opt-in runs. They are
additive: individual `gtest_discover_tests` cases must remain registered for
focused filtering and diagnostics.

## New test naming

New C++ test source files should use the `Test.<Name>.cpp` format.
Do not introduce new `Test_<Name>.cpp` files. Existing older `Test_*.cpp` files
are compatibility carryover and should only be renamed as part of an explicit
mechanical cleanup task.

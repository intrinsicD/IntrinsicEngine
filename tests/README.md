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
  `graphics`, `headless`, `physics`, `platform`, `runtime`.
- capabilities/backends: `glfw`, `gpu`, `vulkan`.
- opt-in filters: `slow`, `flaky-quarantine`.

`tests/CMakeLists.txt` enforces this set at configure time through the test
executable helper. Additions must update this README and the helper's allow-list
in the same change before a new CTest label can be used.

`cmake/IntrinsicTest.cmake` is the authoritative executable/label registry. It
derives these build aggregates from exact label membership and writes their
one-target-per-line inventories under `build/<preset>/test-inventories/`:

| Target | Matching CTest policy |
| --- | --- |
| `IntrinsicTests` | Every registered CTest-producing executable |
| `IntrinsicPrFastTests` | Any `unit` or `contract`; exclude `gpu`, `vulkan`, `slow`, `flaky-quarantine` |
| `IntrinsicCpuTests` | Exclude `gpu`, `vulkan`, `slow`, `flaky-quarantine` |
| `IntrinsicGpuVulkanTests` | Require both `gpu` and `vulkan`; exclude `slow`, `flaky-quarantine` |
| `IntrinsicPrSmokeTests` | Require `integration`, `runtime`, and `graphics`; apply the standard fast exclusions |

Repeated `-L` CTest arguments are intersections, so `IntrinsicGpuVulkanTests`
and `IntrinsicPrSmokeTests` use `INCLUDE_ALL`; the PR-fast
`-L 'unit|contract'` regex is an `INCLUDE_ANY` union. Configuration fails for
undeclared/duplicate labels, duplicate registrations, non-executable producers,
or empty aggregates. `IntrinsicTests` remains the complete local/default
correctness target.

Use `slow` for valid tests that should not run in fast PR or default local CPU
correctness gates, including executables that boot the full headless engine,
initialize Vulkan in a non-`gpu`-only path, run benchmark/SLO thresholds, or
regularly exceed one second of wall-clock time on the reference Linux-clang
runner. Do not add `slow` to pure CPU unit or contract suites merely because
they contain many cheap cases.

Use `flaky-quarantine` only as a temporary quarantine. Any test or executable
with this label must have a linked task ID, a reason, and a removal condition in
the relevant source or task record.

Tests that drive `Engine::Run()` through a platform window must either set
`config.Window.Backend = Core::Config::WindowBackend::Null` for deterministic
headless loop coverage, or guard a born-closed configured/live window with the
established `ShouldClose() -> GTEST_SKIP()` pattern before asserting per-frame
effects. Without one of those choices, headless GLFW hosts execute zero frames
and report unrelated assertion failures instead of an explicit environment skip.

Common gates:

```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

Opt-in promoted Vulkan smoke coverage is selected with the label intersection
form below. `IntrinsicGraphicsVulkanSmokeTests` includes the default-recipe,
HZB conservatism, transient-debug, visualization-overlay, and
`ImGuiSurfaceGpuSmoke.UserTextureImageRecordsOnOperationalVulkanCommandStream`
fixtures; the HZB fixture drives a test-only compute kernel on a real Vulkan
command stream and compares visible/rescued/rejected decisions against
`ComputeTwoPhaseCullPartition(...)`, while the ImGui fixture drives a
runtime-produced `ImGui::Image()`
payload through `ImGuiPass` and verifies that the default-recipe Vulkan
command stream records the pass when the promoted backend is operational. The
default-recipe smoke reports `SKIPPED` when GLFW or a Vulkan-capable
swapchain/device is unavailable and is intentionally excluded from the default
CPU gate by its `gpu;vulkan` labels. Its four-sample pixel assertion runs as a live
`EXPECT_TRUE(Readback::ChannelsWithinTolerance(...))` site on a Vulkan-capable
host: the renderer's opt-in
`SetDefaultRecipeBackbufferReadbackBuffer(handle)` hook + the RHI
`CopyTextureToBuffer`/`ReadBuffer` seam drain the backbuffer into a host-visible
buffer the smoke owns through `BufferManager::Create`. CPU invariants of those
helpers and the renderer wiring (default-disabled state, configured-handle
triplet against MockDevice, non-operational skip path) run inside the default gate via
`IntrinsicGraphicsContractCpuTests` (`MinimalTriangleReadbackHarness`,
`OperationalCounterStability`, `DefaultRecipeBackbufferReadbackContract`).
`DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialReadbackWithValidation`
is the GRAPHICS-119 opt-in Vulkan check: it runs serial and parallel
default-recipe frames with validation enabled, compares the captured readback
bytes, and asserts the graphics-only parallel context plan was accepted without
falling back to the serial path.
`DefaultRecipeSurfaceGpuSmoke.ParallelRecordingMatchesSerialAsyncComputeReadbackWithValidation`
is the companion non-graphics check: it keeps postprocess enabled, requires an
operational async-compute queue profile, compares serial/parallel readback
bytes, and asserts the accepted async-compute parallel context plan did not
fall back to serial.

The opt-in `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` executable (labels
`gpu;vulkan;integration;runtime;graphics`, RUNTIME-095 Slice 3) drives
`Engine::Run()` over the working-sandbox acceptance scene (one mesh, one graph,
one point cloud composed onto the reference camera) with the runtime-owned
`SandboxEditorUi` attached for a bounded number of frames and asserts the
default recipe reaches the canonical `"Present"` pass with no canonical pass
falling through `SkippedUnavailable`, that each
acceptance family resides on its own mesh/graph/point-cloud residency lane
(asserted per lane so the reference triangle's separate `Procedural` lane
cannot mask a broken family), and that the Vulkan fallback counters stay
stable. Like
the other smokes it reports `SKIPPED` when GLFW or an operational Vulkan device
is unavailable, and its CPU/null contracts are covered in the default gate by
the Slice 1/2 `RuntimeSandboxAcceptance.*` cases in
`IntrinsicRuntimeGraphicsCpuTests`.

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

Shared support fixtures live under `support/`. Reusable GPU helpers should stay
engine-free where possible and keep backend-specific setup local to the owning
`gpu;vulkan` fixture or executable.

Repo-tooling fixtures live alongside their consumers. The layering checker
fixtures live under
[`contract/repo/layering_fixtures/`](contract/repo/layering_fixtures/README.md)
and are exercised by `regression/tooling/Test.CheckLayering.py`. Positive
cases live at the top level; negative cases live in sibling `negative_*`
directories and are scanned per-case so the bulk fixture root can stay
clean under `--exclude 'negative_*'`.

## New test naming

New C++ test source files should use the `Test.<Name>.cpp` format.
Do not introduce new `Test_<Name>.cpp` files. Existing older `Test_*.cpp` files
are compatibility carryover and should only be renamed as part of an explicit
mechanical cleanup task.

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
| `IntrinsicGpuVulkanTests` | When Vulkan is configured, require both `gpu` and `vulkan`; exclude `slow`, `flaky-quarantine` |
| `IntrinsicPrSmokeTests` | Require `integration`, `runtime`, and `graphics`; apply the standard fast exclusions |

Repeated `-L` CTest arguments are intersections, so `IntrinsicGpuVulkanTests`
and `IntrinsicPrSmokeTests` use `INCLUDE_ALL`; the PR-fast
`-L 'unit|contract'` regex is an `INCLUDE_ANY` union. Configuration fails for
undeclared/duplicate labels, duplicate registrations, non-executable producers,
or empty aggregates. `IntrinsicTests` remains the complete local/default
correctness target.

Backend-inapplicable aggregates are omitted rather than emitted empty. A
Null/headless configure therefore has no `IntrinsicGpuVulkanTests` target or
inventory; a Vulkan configure still fails closed if that aggregate selects no
registered executable.

The same directory contains two tab-separated audit inventories.
`RegisteredTestTargets.tsv` records each CTest executable and its sorted labels.
`RegisteredTestSources.tsv` has the header columns `source`, `object_library`,
and `target`, and records one sorted row for every repository `Test.*.cpp` or
compatibility `Test_*.cpp` source, the object library that compiles it, and its
sole registered CTest executable. Registering the same assertion-bearing source
to a second executable fails configuration, even when both executables reuse
the same object library. Support sources that do not match the test-source
naming policy are intentionally omitted.

After an aggregate is built, `Test.TestGateRouting.py` reconciles those
inventories with the aggregate file, CTest JSON discovery, and each included
GoogleTest executable's expanded case list. It fails closed on source/case
duplication, label or aggregate drift, missing binaries, and missing or extra
CTest cases. `Test.TestGateRouting.baseline.tsv` additionally pins the exact
233 BUG-106 affected target/case identities so deleting or renaming a test
cannot shrink both live inventories into a false green:

```bash
python3 tests/regression/tooling/Test.TestGateRouting.py \
  --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py \
  --build-dir build/ci-vulkan --aggregate IntrinsicGpuVulkanTests
```

## CPU and sanitizer gate identities

The canonical `ci` preset is unsanitized. Required ASan and UBSan coverage uses
the isolated `ci-asan` and `ci-ubsan` presets, builds only the exact
`IntrinsicCpuTests` aggregate, and applies the same exclusion-only selector:

```bash
cmake --preset ci-asan --fresh
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' \
  --no-tests=error --timeout 60

cmake --preset ci-ubsan --fresh
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' \
  --no-tests=error --timeout 60
```

There is intentionally no positive `-L` filter and no CTest `-j` flag in either
sanitizer command. Regression-only CPU producers remain selected, and `CI-008`
owns any later measured sanitizer worker budget. The two variants use distinct
`build/ci-asan` and `build/ci-ubsan` trees and matching
`external/vcpkg-installed/ci-asan` and
`external/vcpkg-installed/ci-ubsan` package trees.

After all three CPU variants have built `IntrinsicCpuTests`, capture and compare
their exact normalized producer/case inventories:

```bash
python3 tools/ci/cpu_test_selection.py capture \
  --build-dir build/ci --preset ci --expected-sanitizer none \
  --output build/ci/cpu-test-selection.json
python3 tools/ci/cpu_test_selection.py capture \
  --build-dir build/ci-asan --preset ci-asan --expected-sanitizer asan \
  --output build/ci-asan/cpu-test-selection.json
python3 tools/ci/cpu_test_selection.py capture \
  --build-dir build/ci-ubsan --preset ci-ubsan --expected-sanitizer ubsan \
  --output build/ci-ubsan/cpu-test-selection.json
python3 tools/ci/cpu_test_selection.py compare \
  --report build/ci/cpu-test-selection.json \
  --report build/ci-asan/cpu-test-selection.json \
  --report build/ci-ubsan/cpu-test-selection.json \
  --require-sanitizer none --require-sanitizer asan \
  --require-sanitizer ubsan \
  --output build/cpu-test-selection-parity.json
```

Among required CI gates, `ci-vulkan` alone retains combined ASan+UBSan
instrumentation. Its address capability registers the BUG-083 Vulkan shutdown
LeakSanitizer contract; the isolated UBSan CPU tree does not claim
LeakSanitizer coverage. Pull-request and manual `ci-linux-clang` runs call the
reusable sanitizer workflow and require the real three-artifact comparison;
the parity gate does not rebuild or rerun the unsanitized cohort.

Use `slow` for valid tests that should not run in fast PR or default local CPU
correctness gates, including executables that boot the full headless engine,
initialize Vulkan in a non-`gpu`-only path, run benchmark/SLO thresholds, or
regularly exceed one second of wall-clock time on the reference Linux-clang
runner. Do not add `slow` to pure CPU unit or contract suites merely because
they contain many cheap cases.

The 22-result `IntrinsicBenchmarkSmoke.Run` → `.Validate` fixture pair is an
explicit example: it remains in the complete aggregate with a 120-second runner
bound, but is `benchmark;slow` and therefore outside the default CPU-supported
gate. The required `ci-bench-smoke` pull-request workflow still runs the full
aggregate under its own two-minute runner-step bound, strictly validates every
emitted JSON, and retains the complete result directory. The hosted timing
population and ownership rationale are recorded in the
[benchmark CI policy](../docs/benchmarking/ci-policy.md#monolithic-smoke-ownership-and-budget).

Use `flaky-quarantine` only as a temporary quarantine. Any test or executable
with this label must have a linked task ID, a reason, and a removal condition in
the relevant source or task record.

Tests that drive `Engine::Run()` through a platform window must either set
`config.Window.Backend = Core::Config::WindowBackend::Null` for deterministic
headless loop coverage, or guard a born-closed configured/live window with the
established `ShouldClose() -> GTEST_SKIP()` pattern before asserting per-frame
effects. Without one of those choices, headless GLFW hosts execute zero frames
and report unrelated assertion failures instead of an explicit environment skip.

## Capability-truthful target ownership

Executable labels describe requirements shared by every case in that
executable. CPU/mock tests therefore do not share an executable with a test
that requires a live Vulkan backend. The corrected graphics/runtime topology is:

| Executable | Canonical ownership |
| --- | --- |
| `IntrinsicRuntimeIntegrationTests` | Six runtime/core/assets CPU sources, 104 cases, labeled `integration;runtime` |
| `IntrinsicRuntimeContractTests` | Sole CPU owner of the 25 `CoreGraphInterfaces` and `RuntimeEngineLayering` cases, labeled `contract;runtime` |
| `IntrinsicRuntimeGraphicsCpuTests` | Sole owner of `RuntimeFrameLoopContract` and its nine cases, labeled `integration;runtime;graphics` |
| `IntrinsicGraphicsIntegrationCpuTests` | Three MockDevice graphics integration sources, 74 cases, labeled `integration;graphics` |
| `IntrinsicGraphicsUnitTests` | Three MockDevice unit sources, 20 cases, labeled `unit;graphics` |
| `IntrinsicRuntimeGpuReadbackSmokeTests` | The one real readback case, labeled `gpu;vulkan;integration;runtime;graphics;slow` |

`IntrinsicRuntimeGpuReadbackSmokeTests` retains `slow`: current Linux-clang
measurements exceed the documented one-second threshold. Its
`GpuReadbackJobGpuSmoke.VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor`
case remains outside the fast GPU aggregate, but `ci-vulkan` builds it
explicitly and runs it under Xvfb with lavapipe alongside the two operational
Sandbox contracts. The retained JUnit artifact is parsed in the job; an absent,
skipped, or failed readback result fails the gate.

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
one point cloud composed onto the reference camera) with the app-owned
`SandboxEditorController` attached for a bounded number of frames and asserts the
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

`ExtrinsicSandbox.VulkanShutdownLsanContract` is the BUG-083 process-level
regression (`gpu;vulkan;regression;runtime;graphics`). It is intentionally run
under Xvfb/lavapipe in the same operational `ci-vulkan` batch as the readback
smoke, enables LeakSanitizer for an exact five-frame Sandbox process, requires
five renderer-completed samples and an operational final device, then requires
a clean process exit. Its first subprocess reuses the BUG-082 standalone helper
and must still report the named 4096-byte synthetic engine leak under the same
three-entry suppression file. GoogleTest binaries embed no default
suppressions; the three entries are scoped to this runner.

```bash
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^(ExtrinsicSandbox\.(FramePacingDiagnosticCapture|VulkanShutdownLsanContract)|GpuReadbackJobGpuSmoke\.VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor)$' \
  -L 'gpu' -L 'vulkan' --no-tests=error --timeout 180
```

For fast local iteration on changed paths, use the touched-scope helper to plan
or run the relevant subset before the full gate:

```bash
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --run
```

The helper classifies before configure. Structural-only changes skip C++ setup;
known implementation-unit changes use owner-labeled producers from the fresh
`ci-fast` registry; module/header/build/dependency, missing-diff, and unknown
changes fail closed to a bounded broad feedback route that builds PR-fast
before the cross-layer smoke. The smoke remains broad-only because its
configured increment exceeds the declared focused-route budget. The generated
routing artifact records the exact selection. This lane does not replace
final CPU/sanitizer/capability PR/merge verification.

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

# Test Strategy and Classification Policy

This document defines the canonical test classification policy for IntrinsicEngine.

## Test categories

- `unit`: Pure subsystem tests that validate deterministic behavior in isolation.
- `contract`: Tests that validate layer boundaries, API contracts, module rules, and lifecycle invariants.
- `integration`: Tests that compose multiple subsystems (often with fakes/headless runtime wiring).
- `runtime`: Tests that exercise runtime composition, runtime selection, frame orchestration, or runtime services.
- `headless`: Tests that exercise runtime/graphics paths without a visible application window.
- `gpu`: Backend tests requiring GPU/SDK capabilities.
- `vulkan`: Vulkan-backed tests requiring a Vulkan loader, ICD, and compatible device.
- `benchmark`: Benchmark smoke/correctness gates that validate benchmark plumbing and thresholds.
- `slo`: Service-level objective or performance-threshold tests.
- `slow`: Tests that are valid but not appropriate for fast PR or default local correctness gates.
- `flaky-quarantine`: Temporarily quarantined tests with a linked task ID and explicit removal condition.
- `support`: Shared fixtures, builders, helpers, and test utilities used by other test categories.

## Directory mapping

Target directory ownership:

- `tests/unit/` for unit tests by subsystem (`core`, `assets`, `ecs`, `geometry`, `physics`, `graphics`, `runtime`).
- `tests/contract/` for architecture and lifecycle contract checks.
- `tests/integration/` for multi-subsystem tests (headless runtime/app flows).
- `tests/gpu/` for GPU-dependent backend tests.
- `tests/regression/` for bug and numerical regressions.
- `tests/benchmark/` for benchmark smoke/validation tests.
- `tests/support/` for reusable testing support code.

## Test naming convention

All C++ test sources should use:

`Test.<Name>.cpp`

Where:

- `<Name>` starts with the owning layer or subsystem when that is not already obvious from the directory (for example `Core`, `Geometry`, `Runtime`, `Graphics`, or `RHI`).
- Additional dot-separated name segments describe the behavior under test.
- Existing `Test_*.cpp` files are compatibility carryover and should only be renamed by explicit mechanical cleanup tasks that preserve test behavior and CMake ownership.

Examples:

- `Test.Core.LogRingBuffer.cpp`
- `Test.Graphics.FrameGraphContract.cpp`
- `Test.Runtime.HeadlessEngine.cpp`
- `Test.Geometry.VectorHeatRegression.cpp`

## CTest label policy

CTest labels must match both category and ownership to allow targeted CI execution.

Minimum label requirements by directory:

- `tests/unit/<subsystem>/...` -> labels include `unit` and `<subsystem>`.
- `tests/contract/<subsystem-or-topic>/...` -> labels include `contract` and `<subsystem-or-topic>`.
- `tests/integration/<subsystem-or-flow>/...` -> labels include `integration` and `<subsystem-or-flow>`.
- `tests/gpu/<backend>/...` -> labels include `gpu` and `<backend>`; Vulkan tests also include `vulkan`.
- `tests/regression/<topic>/...` -> labels include `regression` and `<topic>`.
- `tests/benchmark/<topic>/...` -> labels include `benchmark` and `<topic>`.
- Runtime/headless tests include `runtime` and `headless` when they exercise runtime composition without a visible window.
- Slow or SLO tests include `slow` or `slo` in addition to their category/owner labels.

Operational expectations:

- The staged `pr-fast` feedback lane resolves the PR base/head merge base and
  classifies the exact merge-base-to-head diff before C++ setup.
  Documentation/task-only routes run structural checks only; known
  implementation-unit routes configure the unsanitized Null/headless
  `ci-fast` preset and select owner-labeled producers; graph-affecting or
  ambiguous routes build the bounded PR-fast plus cross-layer-smoke
  aggregates. Every C++ route reconciles the fresh registry before build.
  The broad unit/contract selector is:

  ```bash
  ctest --test-dir build/ci-fast --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'
  ```

- The default full CPU-supported local/CI gate excludes only capability-heavy, slow, or explicitly quarantined tests:

  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

- For local iteration before the full gate, `tools/ci/touched_scope.py` can plan
  or run conservative affected checks from changed paths:

  ```bash
  python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --print
  python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --run
  ```

  The route artifact records changed files, reasons, fallback state, targets,
  labels, selected cases, command closure, and per-batch timing. This feedback
  lane is not a replacement for the full CPU, sanitizer, or capability-specific
  PR/merge gates.

- GPU/Vulkan tests are opt-in and should run on capable developer machines or self-hosted GPU runners:

  ```bash
  ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 60
  ```

- The hosted `ci-vulkan` job gives tests that require non-skipped operational
  evidence a separate Xvfb/lavapipe lane. Capability-skipping smoke tests remain
  in the generic opt-in batch; the operational lane retains JUnit and fails when
  its required readback case is absent, skipped, or not passed.

- `flaky-quarantine` must not be used as a broad skip. Each quarantined test requires a task ID, a reason, and a removal condition.

## Registry and aggregate integrity

`cmake/IntrinsicTest.cmake` writes three forms of configure-time evidence under
`build/<preset>/test-inventories/`:

- `RegisteredTestTargets.tsv` maps each registered CTest executable to its
  sorted label set.
- `RegisteredTestSources.tsv`, with header columns `source`, `object_library`,
  and `target`, maps every repository `Test.*.cpp` or compatibility
  `Test_*.cpp` source to the object library that compiles it and its one
  registered executable owner.
- Each canonical aggregate has a one-target-per-line `<aggregate>.txt`
  inventory derived from the registry's exact label policy.

The registry rejects a second executable owner for an assertion-bearing source,
including reuse through the same object library. After a build,
`tests/regression/tooling/Test.TestGateRouting.py` independently reselects an
aggregate from target labels and reconciles its inventory, CTest JSON discovery,
and expanded `--gtest_list_tests` output. Duplicate raw cases or CTest filters,
missing/extra aggregate members, label drift, missing binaries, and
GoogleTest/CTest case mismatches fail closed. Its committed
`Test.TestGateRouting.baseline.tsv` pins all 233 affected target/case identities
against simultaneous shrinkage of the live inventories. The required CI lanes
run it for both `IntrinsicCpuTests` and `IntrinsicGpuVulkanTests` before CTest
execution.

## Source coverage evidence

Source coverage is a separate reporting layer, not another test category or a
replacement for correctness. The `ci-coverage-cpu` preset preserves the
canonical Linux/Vulkan/Glfw CPU-contract graph while disabling sanitizers,
Sandbox, benchmark scaffolds, CUDA, and promoted Vulkan operation. Its
`IntrinsicCpuTests` inventory is reconciled before collection.

The reporting lane executes each selected GoogleTest producer once with an
exact filter containing its enabled canonical cases, then reconciles the
machine-readable GoogleTest XML to that exact requested set. This reduces
process and raw-profile overhead without changing CTest registration or the
authoritative case-isolated CPU gate. Manual CTest producers are recorded and
run separately. Every selected binary must emit a collision-safe
`%m-%p.profraw` shard and appear in the `llvm-cov` object set; missing,
uninstrumented, corrupt, or partially selected inputs fail closed. CTest
discovery profiles use a separate retained namespace and never enter the
execution merge.

The normalized report covers engine-owned C++ under `src/` and `methods/`.
Tests, benchmarks, generated build files, assets, vcpkg/external/third-party
code, and compiler runtime sources are excluded deterministically. The retained
artifact records the exact test inventory alongside production source and
build-input digests, the normalized production compile-command digest,
compiler and LLVM-tool versions, preset/backend identity, exclusion policy,
and execution mode. A test-only comparison is valid only when those production
identities match; it then rejects loss of any previously covered region or
either branch outcome even when aggregate coverage is unchanged or higher.

Coverage proves that execution reached mapped source. It does not prove that
an assertion checked the right invariant, that an unexecuted path is wrong, or
that Vulkan/GPU work occurred. Assertion-level correctness remains owned by
tests, and backend operation remains owned by non-skipped capability evidence.

## Capability routing

Labels apply to whole CTest executables, so every case in an executable must
share the declared capability requirements. The BUG-106 correction routes the
former mixed graphics/runtime set as follows:

- `IntrinsicRuntimeIntegrationTests` owns six CPU sources and 104 cases under
  `integration;runtime`.
- `IntrinsicRuntimeContractTests` owns the 25 `CoreGraphInterfaces` and
  `RuntimeEngineLayering` cases under `contract;runtime`.
- `IntrinsicRuntimeGraphicsCpuTests` is the sole owner of
  `RuntimeFrameLoopContract` and its nine cases under
  `integration;runtime;graphics`.
- `IntrinsicGraphicsIntegrationCpuTests` owns three MockDevice integration
  sources and 74 cases under `integration;graphics`.
- `IntrinsicGraphicsUnitTests` owns three MockDevice unit sources and 20 cases
  under `unit;graphics`.
- `IntrinsicRuntimeGpuReadbackSmokeTests` owns only
  `GpuReadbackJobGpuSmoke.VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor`
  under `gpu;vulkan;integration;runtime;graphics;slow`.

The dedicated readback executable retains `slow` because current Linux-clang
measurements exceed the one-second policy threshold. It remains outside both
the CPU gate and fast GPU aggregate. `ci-vulkan` builds it explicitly, runs it
under Xvfb/lavapipe with the two operational Sandbox contracts, and retains
machine-checked JUnit proving the case executed without a skip.

## Configure identity and inventory determinism

Test target selection is meaningful only when identical configure inputs
produce an identical target graph. The root `CMakeLists.txt` owns
`EXTRINSIC_PLATFORM` and `EXTRINSIC_BACKEND` before the renderer consumes
them; the platform layer resolves `INTRINSIC_PLATFORM_BACKEND` without
redefining those inputs. Current CI presets pin the intended Linux/Vulkan
identity, while explicit local Null/headless and Glfw/Vulkan configurations
remain supported.

`tests/regression/tooling/Test.BackendConfigureDeterminism.py` compares
normalized first-fresh, unchanged-reconfigure, and second-fresh target,
generated aggregate, and CTest inventories. A deliberate identity change may
change the graph; configure history may not. CI timing results receive the
same configured identity from the build tree's `CMakeCache.txt`, so inventory
and timing evidence can be attributed to the graph that produced it.

## Migration notes

- During migration, temporary compatibility entries in `tests/CMakeLists.txt` are allowed.
- Reclassified tests must update both file location and labels in the same change.
- Any temporary category mismatch must be tracked in a current task under `tasks/active/` with a removal condition.
- BUG-106 replaced the former mixed graphics/runtime wrapper target with the
  capability-truthful CPU and readback topology documented above. Future moves
  must preserve its unique source and expanded-case ownership.

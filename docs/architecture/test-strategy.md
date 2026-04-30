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

- `tests/unit/` for unit tests by subsystem (`core`, `assets`, `ecs`, `geometry`, `graphics`, `runtime`).
- `tests/contract/` for architecture and lifecycle contract checks.
- `tests/integration/` for multi-subsystem tests (headless runtime/app flows).
- `tests/gpu/` for GPU-dependent backend tests.
- `tests/regression/` for bug and numerical regressions.
- `tests/benchmark/` for benchmark smoke/validation tests.
- `tests/support/` for reusable testing support code.

## Test naming convention

All C++ test sources should use:

`Test_<Subsystem>_<Feature>_<Kind>.cpp`

Where:

- `<Subsystem>` is the owning layer or subsystem (for example `Core`, `Geometry`, `Runtime`, `Graphics`).
- `<Feature>` describes the behavior under test.
- `<Kind>` is a short category cue (`Unit`, `Contract`, `Integration`, `Gpu`, `Regression`, `Benchmark`).

Examples:

- `Test_Core_LogRingBuffer_Unit.cpp`
- `Test_Graphics_FrameGraph_Contract.cpp`
- `Test_Runtime_HeadlessEngine_Integration.cpp`
- `Test_Geometry_VectorHeat_Regression.cpp`

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

- Fast PR checks run unit and contract tests:

  ```bash
  ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'
  ```

- The default full CPU-supported local/CI gate excludes only capability-heavy, slow, or explicitly quarantined tests:

  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

- GPU/Vulkan tests are opt-in and should run on capable developer machines or self-hosted GPU runners:

  ```bash
  ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 60
  ```

- `flaky-quarantine` must not be used as a broad skip. Each quarantined test requires a task ID, a reason, and a removal condition.

## Migration notes

- During migration, temporary compatibility entries in `tests/CMakeLists.txt` are allowed.
- Reclassified tests must update both file location and labels in the same change.
- Any temporary category mismatch must be tracked in a current task under `tasks/active/` with a removal condition.
- HARDEN-041B registers relocated wrapper sources under taxonomy-owned targets (`IntrinsicAssetUnitTests`, `IntrinsicCoreWrapperUnitTests`, `IntrinsicGraphicsUnitTests`, `IntrinsicGraphicsContractTests`, `IntrinsicRuntimeIntegrationTests`) instead of subsystem wrapper directories.
- Graphics/runtime relocated wrapper suites are currently labeled `gpu`/`vulkan` because they include backend-facing coverage; CPU-only relabeling/splits remain follow-up work under HARDEN-042.

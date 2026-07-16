---
id: BUG-106
theme: G
depends_on:
  - CI-004
---
# BUG-106 — Test-gate capability routing hides CPU coverage

## Goal
- Restore one canonical, capability-truthful test graph in which CPU/mock
  cases run in the default CPU gate, the real Vulkan readback case stays
  opt-in, and no GoogleTest case is registered through two executables.

## Non-goals
- No test-body or production-engine behavior change.
- No new CTest label or one-executable-per-test-file fragmentation.
- No grouped CTest execution, worker-budget tuning, or broad slow-test
  reclassification; those remain `CI-008` and `CI-011`.
- No backend-default or configure-order change; that defect is `BUG-107`.

## Context
- Symptom: `RuntimeIntegrationTestObjs` contains thirteen sources but only
  `Test.GpuReadbackJobGpuSmoke.cpp` imports the promoted Vulkan backend. The
  other twelve sources contain 212 CPU/mock/source-contract `TEST*`
  definitions, yet the whole executable is labeled
  `integration;runtime;graphics;gpu;vulkan;slow`.
- Symptom: the default CPU selector excludes that mixed target because of its
  capability labels, while the current PR Vulkan selector excludes it because
  of `slow`. The CPU-oriented cases therefore do not belong to either standard
  correctness lane.
- Symptom: `Test.RuntimeFrameLoopContract.cpp` is listed in both
  `RuntimeGraphicsCpuTestObjs` and `RuntimeIntegrationTestObjs`, duplicating its
  nine cases when both executables are run.
- Symptom: the three `GraphicsUnitTestObjs` sources contain twenty MockDevice
  unit cases but `IntrinsicGraphicsUnitTests` is labeled `gpu;vulkan`, moving
  CPU-only coverage into the expensive capability gate.
- Expected behavior: capability labels describe requirements of every case in
  an executable, every expected case has one canonical owner, and the CPU and
  Vulkan build aggregates select the same targets that CTest will execute.
- Impact: ordinary CPU regressions can escape required gates, Vulkan CI spends
  time on mock-only tests, and all-test runs can execute duplicate assertions.
- Retired `BUG-102` recorded the practical consequence: its source-layering
  regression was absent from the default CPU selector because the containing
  executable carried `gpu;vulkan;slow`, while that narrowly scoped repair
  correctly forbade relabeling. This task owns the broader registration fix.
- Retired `CI-004` provides the canonical target/label registry. The stale
  migration note assigning CPU relabeling to retired `HARDEN-042` has no live
  owner and must be replaced by this task.

## Required changes
- [ ] Capture the pre-change source-to-object-library, executable-to-label,
      and fully expanded GoogleTest case inventory for every affected source.
- [ ] Move the twelve CPU/mock/source-contract sources out of the mixed
      Vulkan/slow object library into existing CPU targets where their link
      dependencies fit; add the smallest dependency-coherent CPU executable
      only where reuse would broaden an existing target incorrectly.
- [ ] Keep `Test.GpuReadbackJobGpuSmoke.cpp` in a dedicated
      `gpu;vulkan;integration;runtime;graphics` executable with `slow` only if
      current timing evidence satisfies the documented policy.
- [ ] Register `Test.RuntimeFrameLoopContract.cpp` through exactly one CPU
      executable.
- [ ] Remove `gpu;vulkan` from `IntrinsicGraphicsUnitTests` after proving all
      three sources remain MockDevice-only and require no live backend.
- [ ] Extend the canonical test-registry tooling or a focused regression so
      duplicate source ownership, duplicate expanded case names, label/build
      aggregate disagreement, and missing expected cases fail closed.
- [ ] Keep object-library reuse and executable granularity dependency-driven;
      do not create a new registration framework.

## Tests
- [ ] Add `tests/regression/tooling/Test.TestGateRouting.py` (or an equivalently
      focused existing-tool extension) covering unique source/case ownership,
      CPU/GPU capability truth, and aggregate membership.
- [ ] Compare pre/post `--gtest_list_tests` inventories and prove the distinct
      assertion-bearing case set is unchanged while duplicate executions are
      removed.
- [ ] Build and run the reclassified cases through the default CPU selector.
- [ ] Build the dedicated readback executable through `ci-vulkan` and run its
      opt-in test on a capable hosted runner, retaining at least one non-skipped
      passing result; a local capability skip validates registration only and
      cannot close operational evidence.
- [ ] Run the complete CPU-supported correctness gate and strict test-layout
      check.

## Docs
- [ ] Update `tests/README.md` and `docs/architecture/test-strategy.md` with the
      corrected CPU/GPU ownership and replace the stale `HARDEN-042` follow-up
      reference with `BUG-106`.
- [ ] Update task/category indexes and regenerate `tasks/SESSION-BRIEF.md` on
      retirement.

## Acceptance criteria
- [ ] Every affected GoogleTest case has one canonical executable owner and no
      test body was deleted or weakened.
- [ ] All 212 CPU-oriented mixed-target definitions and the twenty MockDevice
      unit definitions are selected by the default CPU-supported gate.
- [ ] The one real Vulkan readback definition remains selected by the intended
      `gpu;vulkan` lane and not by the CPU lane.
- [ ] At least one retained `ci-vulkan` result executes the dedicated readback
      test non-skipped and passes; registration-only or capability-skip evidence
      is not sufficient.
- [ ] `Test.RuntimeFrameLoopContract.cpp` is compiled and executed exactly once
      per complete configured test run.
- [ ] Build-aggregate inventories and CTest label selection are mechanically
      consistent and fail closed on a synthetic mismatch.

## Verification
```bash
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan --fresh
cmake --build --preset ci-vulkan --target IntrinsicGpuVulkanTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -LE 'flaky-quarantine' --no-tests=error --timeout 120
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding the CPU cases behind a different opt-in label or dropping them from a
  required/scheduled lane.
- Running both grouped and individual copies to manufacture case-count parity.
- Adding production test seams or changing renderer/runtime behavior to repair
  CMake ownership.
- Treating a skipped Vulkan smoke as evidence that the backend path executed.

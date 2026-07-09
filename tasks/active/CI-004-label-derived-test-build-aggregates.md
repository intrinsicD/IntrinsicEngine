---
id: CI-004
theme: H
depends_on:
  - CI-003
---
# CI-004 — Build only the test executables selected by each gate

## Status
- In progress on branch `copilot/ci-004-test-build-aggregates`.
- Owner/agent: GitHub Copilot CLI.
- Current slice: inspect registration metadata and define one authoritative
  aggregate derivation path.
- Next verification step: prove current target/label selection semantics against
  the configured CTest inventory before changing workflow targets.

## Goal
- Derive gate-specific aggregate build targets from the same CTest label
  metadata used for execution so PR-fast and Vulkan jobs stop compiling test
  executables they cannot run.

## Non-goals
- No touched-file routing; `CI-005` owns that workflow change.
- No removal, relabeling, grouping, or consolidation of tests.
- No sharing of build trees or compiled module artifacts between jobs.

## Context
- Owner: `tests/CMakeLists.txt`, CMake test helpers, prerequisite validation,
  and compile-heavy workflows.
- `intrinsic_test_executable()` records each test target and its labels in
  global properties, but the final `IntrinsicTests` aggregate depends on every
  registered executable.
- Representative observations from `CI-003`: `pr-fast` built for ~20m06s to
  run 3,526 unit/contract cases, while `ci-vulkan` built
  `ExtrinsicSandbox IntrinsicTests` for ~22m21s to run only 60
  `gpu;vulkan` cases in 3.11s. Both jobs compile the full test aggregate.
- CTest's documented Vulkan selector is an intersection:
  `-L gpu -L vulkan -LE 'slow|flaky-quarantine'`. Build-target derivation must
  reproduce that semantics at executable granularity rather than treating
  labels as one union regex.
- `tools/ci/check_prerequisites.py` already validates expected binaries before
  tests. The new aggregates must remain compatible with that fail-closed check.

## Required changes
- [ ] Refactor test registration metadata into one helper that can derive build
      aggregates and CTest selection expectations from the same target/label
      declarations.
- [ ] Add stable aggregate targets for the exact supported gates:
      `IntrinsicPrFastTests`, `IntrinsicCpuTests`,
      `IntrinsicGpuVulkanTests`, and a deliberately small
      `IntrinsicPrSmokeTests` used by `CI-005`.
- [ ] Make label include/exclude/intersection semantics explicit and fail
      configuration when an aggregate references undeclared labels or a
      registered target has ambiguous metadata.
- [ ] Change `pr-fast` and `ci-vulkan` to build their gate-specific aggregate
      rather than `IntrinsicTests`; keep `ExtrinsicSandbox` explicit where the
      operational Vulkan smoke requires it.
- [ ] Keep `IntrinsicTests` as the canonical complete local/default aggregate.
- [ ] Extend prerequisite checks so a missing executable selected by the CTest
      filter fails before CTest, while an intentionally unselected executable
      is not required.

## Slice plan
- **Slice A.** Centralize target/label metadata and derive the four stable
  aggregate targets with configuration-time validation and exact tooling
  regression coverage. Preserve all workflow targets.
- **Slice B.** Route PR-fast and Vulkan workflows to their aggregates, align
  prerequisite validation, and update canonical test/build documentation.
- **Slice C.** Record comparable cold-build/Ninja-edge evidence against the
  CI-003 aggregate baseline, run the specialized selectors, and retire the task.

## Tests
- [ ] Add CMake/tooling regression coverage that enumerates registered targets
      and proves each gate's build set exactly contains its selected executable
      set.
- [ ] Prove the GPU/Vulkan aggregate implements label intersection and excludes
      `slow`/`flaky-quarantine` targets.
- [ ] Run the PR-fast and GPU/Vulkan selectors after building only their new
      aggregates; no selected test may be absent.
- [ ] Record cold build duration and Ninja edge count against the `CI-003`
      baseline; report the delta without adding a timing threshold from one run.

## Docs
- [ ] Document aggregate target meanings and their relationship to CTest label
      selectors in `tests/README.md`.
- [ ] Update workflow/build documentation that currently prescribes
      `IntrinsicTests` for specialized gates; retain it for the default CPU
      correctness gate.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] `pr-fast` and `ci-vulkan` no longer build the complete
      `IntrinsicTests` aggregate.
- [ ] Every test selected by each specialized CTest command has its executable
      built, and no unrelated test executable is pulled in by the aggregate.
- [ ] The default `IntrinsicTests` aggregate and full CPU gate retain complete
      coverage.
- [ ] Comparable telemetry demonstrates the build-edge and wall-time delta
      against `ci.gate-latency.github-ubuntu-24.04.v1`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicPrFastTests IntrinsicCpuTests IntrinsicPrSmokeTests
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci --skip-undeclared
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGpuVulkanTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -LE 'slow|flaky-quarantine' --timeout 120 -j$(nproc)
python3 tests/regression/tooling/Test.TestBuildAggregates.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hand-maintaining a second target list that can drift from test labels.
- Treating repeated `-L` CTest selectors as a union.
- Removing tests or changing their assertions to reduce the aggregate.
- Replacing Ninja or caching build/BMI trees.

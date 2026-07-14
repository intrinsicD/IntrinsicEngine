---
id: CI-004
theme: H
depends_on:
  - CI-003
---
# CI-004 — Build only the test executables selected by each gate

## Status
- Completed 2026-07-10 at `Operational` on `main`.
- Implementation commit references: `e741293d`, `bc5c7cea`, and `99c579e0`;
  retirement recorded in this local commit.
- Hosted `pr-fast` and `ci-vulkan` artifacts supply the missing Slice C
  cold-build/edge evidence. PR-fast compiled fewer edges without a measured
  wall-time win; Vulkan compiled materially fewer edges and completed its build
  substantially faster than the CI-003 cold baseline.

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
- [x] Refactor test registration metadata into one helper that can derive build
      aggregates and CTest selection expectations from the same target/label
      declarations.
- [x] Add stable aggregate targets for the exact supported gates:
      `IntrinsicPrFastTests`, `IntrinsicCpuTests`,
      `IntrinsicGpuVulkanTests`, and a deliberately small
      `IntrinsicPrSmokeTests` used by `CI-005`.
- [x] Make label include/exclude/intersection semantics explicit and fail
      configuration when an aggregate references undeclared labels or a
      registered target has ambiguous metadata.
- [x] Change `pr-fast` and `ci-vulkan` to build their gate-specific aggregate
      rather than `IntrinsicTests`; keep `ExtrinsicSandbox` explicit where the
      operational Vulkan smoke requires it.
- [x] Keep `IntrinsicTests` as the canonical complete local/default aggregate.
- [x] Extend prerequisite checks so a missing executable selected by the CTest
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
- [x] Add CMake/tooling regression coverage that enumerates registered targets
      and proves each gate's build set exactly contains its selected executable
      set.
- [x] Prove the GPU/Vulkan aggregate implements label intersection and excludes
      `slow`/`flaky-quarantine` targets.
- [x] Run the PR-fast and GPU/Vulkan selectors after building only their new
      aggregates; no selected test may be absent.
- [x] Record cold build duration and Ninja edge count against the `CI-003`
      baseline; report the delta without adding a timing threshold from one run.

## Docs
- [x] Document aggregate target meanings and their relationship to CTest label
      selectors in `tests/README.md`.
- [x] Update workflow/build documentation that currently prescribes
      `IntrinsicTests` for specialized gates; retain it for the default CPU
      correctness gate.
- [x] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] `pr-fast` and `ci-vulkan` no longer build the complete
      `IntrinsicTests` aggregate.
- [x] Every test selected by each specialized CTest command has its executable
      built, and no unrelated test executable is pulled in by the aggregate.
- [x] The default `IntrinsicTests` aggregate and full CPU gate retain complete
      coverage.
- [x] Comparable telemetry demonstrates the build-edge and wall-time delta
      against `ci.gate-latency.github-ubuntu-24.04.v1`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicPrFastTests IntrinsicCpuTests IntrinsicPrSmokeTests
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci --inventory build/ci/test-inventories/IntrinsicPrFastTests.txt
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGpuVulkanTests
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci-vulkan --inventory build/ci-vulkan/test-inventories/IntrinsicGpuVulkanTests.txt
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci-vulkan --targets ExtrinsicSandbox
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -LE 'slow|flaky-quarantine' -E '^ExtrinsicSandbox\.FramePacingDiagnosticCapture$' --timeout 120 -j$(nproc)
xvfb-run -a --server-args='-screen 0 1280x720x24' \
  ctest --test-dir build/ci-vulkan --output-on-failure \
    -R '^ExtrinsicSandbox\.FramePacingDiagnosticCapture$' \
    -L gpu -L vulkan --no-tests=error --timeout 180
python3 tests/regression/tooling/Test.TestBuildAggregates.py
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice A verification run on 2026-07-09:

```bash
python3 tests/regression/tooling/Test.TestBuildAggregates.py -v
cmake --preset ci
cmake --build --preset ci --target IntrinsicPrFastTests IntrinsicCpuTests IntrinsicGpuVulkanTests IntrinsicPrSmokeTests
ctest --test-dir build/ci --show-only=json-v1 -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'
ctest --test-dir build/ci --show-only=json-v1 -LE 'gpu|vulkan|slow|flaky-quarantine'
ctest --test-dir build/ci --show-only=json-v1 -L gpu -L vulkan -LE 'slow|flaky-quarantine'
ctest --test-dir build/ci --show-only=json-v1 -L integration -L runtime -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine'
```

The configured `ci` registry contains 30 executable targets. Independent
selector-to-command comparison matched all generated inventories exactly:
PR-fast selects 3,576 tests / 19 executables, CPU selects 3,635 / 22,
GPU+Vulkan selects 59 / 5 after excluding the explicit Sandbox capture, and
PR-smoke selects 51 / 1.

Slice B verification run on 2026-07-09:

```bash
python3 tests/regression/tooling/Test_CiPrerequisiteGuards.py -v
python3 tests/regression/tooling/Test.WorkflowConcurrency.py -v
python3 tests/regression/tooling/Test.TestBuildAggregates.py -v
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci --inventory build/ci/test-inventories/IntrinsicPrFastTests.txt
python3 tools/ci/check_prerequisites.py test-binaries --build-dir build/ci --inventory build/ci/test-inventories/IntrinsicGpuVulkanTests.txt
python3 -c 'from pathlib import Path; import yaml; [yaml.safe_load(path.read_text()) for path in Path(".github/workflows").glob("*.yml")]'
git diff --check
```

Slice C local verification run on 2026-07-09:

```bash
cmake --build --preset ci --target IntrinsicPrFastTests
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
# 3,576/3,576 passed in 52.08 s.

cmake --build --preset ci-vulkan --target ExtrinsicSandbox IntrinsicGpuVulkanTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -LE 'slow|flaky-quarantine' -E '^ExtrinsicSandbox\.FramePacingDiagnosticCapture$' --timeout 120 -j$(nproc)
# 59/59 passed in 24.10 s on NVIDIA GeForce RTX 3050, driver 590.48.01.

cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L integration -L runtime -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
# Complete aggregate built; PR-smoke selector passed 51/51 in 3.98 s.
```

Independent `ninja -t commands` closure counts provide deterministic structural
evidence without making a local wall-time claim:

| Build closure | Command edges | Delta from prior complete closure |
|---|---:|---:|
| `IntrinsicTests` (`ci`) | 2,180 | baseline |
| `IntrinsicPrFastTests` (`ci`) | 1,941 | -239 (-11.0%) |
| `IntrinsicCpuTests` (`ci`) | 2,003 | -177 (-8.1%) |
| `IntrinsicPrSmokeTests` (`ci`) | 1,355 | -825 (-37.8%) |
| `ExtrinsicSandbox IntrinsicTests` (`ci-vulkan`) | 2,188 | baseline |
| `ExtrinsicSandbox IntrinsicGpuVulkanTests` (`ci-vulkan`) | 1,492 | -696 (-31.8%) |

The hosted `CI-003` cold-build medians remain 1,381 s for `pr-fast` and
1,399 s for `ci-vulkan`. Comparable post-change wall time must come from the
same `ubuntu-24.04` workflows after publication; the local edge counts above
are not presented as a hosted performance result.

The separate `ExtrinsicSandbox.FramePacingDiagnosticCapture` executable passed
the fail-closed prerequisite check but could not execute locally because
`xvfb-run` is absent and this host requires an interactive sudo password.
`ci-vulkan.yml` installs `xvfb` before running that isolated capture.

The default CPU selector enumerated all 3,635 expected tests and completed
3,634 before one incremental-tree ASan failure in
`CoreTasks.CoroutineDispatch`. It then passed 25 focused repetitions in the
incremental tree. Stale-build triage configured a fresh
`build/ci-coretasks-clean` preset tree, built
`IntrinsicCoreWrapperUnitTests` with `CCACHE_DISABLE=1` under Clang 23, and
passed 100 sequential plus 100 concurrent-process focused runs. Per the
stale-build policy, this is recorded as a stale module-artifact incident; no
speculative scheduler source change or bug task is warranted.

Slice C hosted verification recorded on 2026-07-10 from the published
`origin/main` workflow artifacts:

| Gate | Run / artifact | Build time | Hosted edges | Baseline delta |
|---|---|---:|---:|---:|
| `pr-fast` | [29079330857](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29079330857) / `8222675239` | 1,416.611 s | 1,955 | +2.6% time; -10.3% edges |
| `pr-fast` | [29083843623](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29083843623) / `8224514366` | 1,438.647 s | 1,955 | +4.2% time; -10.3% edges |
| `ci-vulkan` | [29079330858](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29079330858) / `8222434441` | 973.955 s | 1,475 | -30.4% time; -32.6% edges |
| `ci-vulkan` | [29082107754](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29082107754) / `8223466674` | 707.794 s | 1,475 | -49.4% time; -32.6% edges |
| `ci-vulkan` | [29083843657](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29083843657) / `8224168737` | 701.741 s | 1,475 | -49.8% time; -32.6% edges |

The comparison uses the checked-in CI-003 cold-build medians (1,381 s
`pr-fast`, 1,399 s `ci-vulkan`) and prior complete aggregate closures (2,180
and 2,188 edges respectively). A third PR-fast build from run `29082108175`
completed in 1,427.436 s with 1,955 edges, but concurrency cancellation left
its test phase report missing; it is diagnostic context, not part of the
complete-result comparison. The two complete PR-fast samples do **not** support
a speedup claim. Every Vulkan configure/build and main `gpu;vulkan` selector
passed; only the isolated frame-pacing capture failed, which remains owned by
`BUG-064` and does not invalidate the CI-003 build-phase comparison policy.

## Completion

- Completed: 2026-07-10. Commit references: `e741293d`, `bc5c7cea`, `99c579e0`;
  task retirement recorded in this local commit.
- Maturity: `Operational` in hosted `pr-fast` and `ci-vulkan` workflows.
- No timing threshold was added from this small sample, and no PR-fast speedup
  is claimed.

## Forbidden changes
- Hand-maintaining a second target list that can drift from test labels.
- Treating repeated `-L` CTest selectors as a union.
- Removing tests or changing their assertions to reduce the aggregate.
- Replacing Ninja or caching build/BMI trees.

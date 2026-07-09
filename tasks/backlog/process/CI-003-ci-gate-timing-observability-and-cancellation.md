---
id: CI-003
theme: H
depends_on: []
---
# CI-003 — Make CI gate latency observable and cancel stale runs

## Goal
- Publish comparable, machine-readable configure/build/test/total timing for
  every required CI gate and cancel superseded runs so latency work has a stable
  baseline and obsolete commits stop consuming runner capacity.

## Non-goals
- No build, test-selection, sanitizer, or runner-size optimization in this
  task; those changes belong to `CI-004..009`.
- No performance claim from one representative run.
- No cache of CMake build trees, Ninja state, object files, or C++ module BMIs.

## Context
- Owner: CI/tooling and benchmark reporting. This is the measurement foundation
  for `CI-004..009` and the compile-hotspot tasks linked below.
- The 2026-07-09 audit captured these representative GitHub-hosted
  `ubuntu-24.04` observations. They are sufficient to avoid repeating the
  exploratory diagnosis, but are single samples rather than a claim-grade
  baseline:

  | Gate | Run / job | Configure | Build | Test | Total | Selected tests |
  | --- | --- | ---: | ---: | ---: | ---: | ---: |
  | `pr-fast` | `29044370625` / `86209230534` | 28.767 s | ~20m06s | 221.27 s | 25m00s | 3,526 |
  | `ci-linux-clang` | `29042046463` / `86201390059` | 14.476 s | ~20m47s | 217.53 s | 25m10s | 3,594 |
  | UBSan | `29044370615` / `86209230508` | 7.306 s | ~14m40s | 110.62 s | 17m16s | 3,592 |
  | `ci-vulkan` | `29044370630` / `86209230943` | 10.102 s | ~22m21s | 3.11 s | 23m03s | 60 |
  | benchmark smoke | `29044370620` / `86209230558` | 10.530 s | ~6m49s first build | n/a | 7m51s | n/a |
  | docs validation | same PR cohort | n/a | n/a | n/a | ~13 s | n/a |

- Compilation consumed roughly 80–97% of non-doc gate wall time. Configure and
  vcpkg handling are not the primary bottleneck.
- Current workflows time only configure through `tools/ci/time_command.py`.
  Build/test phases, selected test count, Ninja edge count, cache statistics,
  preset, runner identity, and commit SHA are not emitted as one result.
- The same PR commit launches independent cold builds in `pr-fast`,
  `ci-linux-clang`, ASan, UBSan, `ci-vulkan`, and benchmark smoke. The workflows
  have no `concurrency`/`cancel-in-progress` policy.
- Compile-hotspot observations from the same audit, retained here so source
  owners do not need to repeat discovery:

  | Source | Compile | Lines | Imports/exports | Owner |
  | --- | ---: | ---: | ---: | --- |
  | `Runtime.SandboxEditorUi.cppm` | 159.174 s | 2,899 | 44 | `ARCH-006` |
  | `Runtime.Engine.cppm` | 140.072 s | 1,153 | 50 | `RUNTIME-151` |
  | `Runtime.RenderExtraction.cppm` | 106.935 s | 867 | 36 | `RUNTIME-152` |
  | `Test.SandboxEditorUi.cpp` | 96.017 s | audit artifact | n/a | `ARCH-006` |
  | `Sandbox.cppm` | 92.724 s | 54 | audit artifact | `ARCH-006` |
  | `Runtime.Engine.cpp` | 86.816 s | 6,529 | audit artifact | `RUNTIME-146..150` |
  | `Runtime.SandboxEditorUi.cpp` | 80.264 s | 24,807 | audit artifact | `ARCH-006` |
  | `Geometry.Linalg.cpp` | 45.725 s | audit artifact | audit artifact | monitor after primary splits |

- Historical local evidence in
  `tools/analysis/build_time_baseline_2026-04-05.md` recorded an 11m24s clean
  build on four cores, a 0.125s no-op build, a 16s implementation-only touch,
  and 3m23s–4m19s module-interface touches. The operational rule remains:
  non-trivial implementation belongs outside `.cppm` interfaces.

## Required changes
- [ ] Extend the existing timing wrapper/reporting path so configure, build,
      test, and total workflow phases emit one schema-versioned JSON result plus
      a GitHub step summary.
- [ ] Give the telemetry series a stable identity
      (`ci.gate-latency.github-ubuntu-24.04.v1`) and declare gate, preset,
      compiler, sanitizer, runner image, SHA, cold/warm state, selected test
      count, Ninja edge count, ccache hit/miss counts, phase durations, and
      diagnostics in a repository benchmark manifest/result pair validated
      under the benchmark policy.
- [ ] Persist the result as a small workflow artifact; do not persist the build
      tree or BMIs.
- [ ] Add workflow-level concurrency groups keyed by workflow plus PR/ref, with
      `cancel-in-progress: true`, to PR-triggered compile-heavy workflows.
- [ ] Seed the formal baseline from the run/job IDs above and at least five
      comparable samples per gate (historical logs are acceptable); report
      median and p95 separately for cold and warm-cache observations.
- [ ] Keep the captured table and the formal baseline artifact linked from the
      implementation task/benchmark report so future sessions do not rerun the
      exploratory audit.

## Tests
- [ ] Add regression coverage for successful/failed command timing, JSON
      schema/version, unit conversion, and diagnostic propagation.
- [ ] Add a workflow-policy regression proving every compile-heavy PR workflow
      has the intended concurrency group and wraps all measured phases.
- [ ] Validate any benchmark manifest/result fixture through the repository
      validators.

## Docs
- [ ] Document the telemetry identity, measurement conditions, cold/warm
      distinction, artifact location, and baseline-comparison rule in
      `docs/benchmarking/ci-policy.md` or the nearest canonical CI performance
      document.
- [ ] Update the CI workflow documentation with stale-run cancellation
      semantics.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` when retiring this task.

## Acceptance criteria
- [ ] Every compile-heavy required gate publishes machine-readable phase
      timings and the contextual counters needed for a comparable result.
- [ ] A baseline report contains at least five comparable samples per gate and
      reports median/p95 without presenting the representative table as a
      performance claim.
- [ ] Pushing a newer commit to one PR cancels older in-progress runs of the
      same workflow without cancelling another PR or the default branch.
- [ ] No build tree, BMI, or object cache is introduced.

## Verification
```bash
python3 tests/regression/tooling/Test.CiTiming.py
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
# After downloading or producing a CI timing artifact:
python3 tools/benchmark/validate_benchmark_results.py --root <ci-timing-result-dir> --strict
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Claiming a percentage improvement without a named comparable baseline.
- Combining cold and warm-cache samples in one statistic.
- Optimizing configure first when build remains the dominant phase.
- Caching build directories, compiler BMIs, or Ninja dependency state.
- Weakening or skipping any current correctness gate.

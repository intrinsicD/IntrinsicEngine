---
id: CI-003
theme: H
depends_on: []
---
# CI-003 — Make CI gate latency observable and cancel stale runs

## Status
- Completed on 2026-07-09 at `CPUContracted`.
- Commit: `f168f15d` (result contract), `78c4b152` (workflow instrumentation
  and cancellation), and `fbc3ae9d` (historical aggregate baseline).
- No `Operational` follow-up is owed: this process task's endpoint is
  repository-validated workflow policy. Post-publication timing artifacts are
  measurements consumed by `CI-004..009`, not missing implementation.
- Remote publication remains environment-blocked until the GitHub OAuth token
  has `workflow` scope; the completed commits remain local meanwhile.

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
  | `Runtime.RenderExtraction.cppm` | 106.935 s | 867 | 36 | `RUNTIME-166` |
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
- The formal pre-optimization baseline is
  [`benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json`](../../benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json).
  Its distinct `ci.gate-latency.github-ubuntu-24.04.v1.aggregate-baseline`
  identity links back to the per-run profile without impersonating one gate
  result. It uses the same five pull-request commits for every population,
  retains all 30 job and 25 run IDs, and records the completed authenticated API audit.
  All samples are cold compiles with warm vcpkg binary-package caches; no warm
  compile population existed. The pre-BUG-064 Vulkan test/total statistics are
  diagnostic only, while configure/build remain comparable.

## Slice plan
- **Slice A.** Define the stable benchmark manifest/result contract, extend the
  timing tooling to aggregate phase JSON into one validated gate result, and
  add regression fixtures for success/failure/units/diagnostics. No workflow
  routing changes.
- **Slice B.** Add concurrency cancellation, wrap every compile-heavy workflow
  phase, publish one result artifact per gate, and add workflow-policy
  regressions. No gate selection or sanitizer topology changes.
- **Slice C.** Backfill at least five comparable historical samples per gate,
  publish cold/warm median and p95 baseline evidence, update benchmarking/CI
  docs, and retire the task.

## Required changes
- [x] Extend the existing timing wrapper/reporting path so configure, build,
      test, and total workflow phases emit one schema-versioned JSON result plus
      a GitHub step summary.
- [x] Give the telemetry series a stable identity
      (`ci.gate-latency.github-ubuntu-24.04.v1`) and declare gate, preset,
      compiler, sanitizer, runner image, SHA, cold/warm state, selected test
      count, Ninja edge count, ccache hit/miss counts, phase durations, and
      diagnostics in a repository benchmark manifest/result pair validated
      under the benchmark policy.
- [x] Persist the result as a small workflow artifact; do not persist the build
      tree or BMIs.
- [x] Add workflow-level concurrency groups keyed by workflow plus PR/ref, with
      `cancel-in-progress: true`, to PR-triggered compile-heavy workflows.
- [x] Seed the formal baseline from the run/job IDs above and at least five
      comparable samples per gate (historical logs are acceptable); report
      median and p95 separately for cold and warm-cache observations.
- [x] Keep the captured table and the formal baseline artifact linked from the
      implementation task/benchmark report so future sessions do not rerun the
      exploratory audit.

## Tests
- [x] Add regression coverage for successful/failed command timing, JSON
      schema/version, unit conversion, and diagnostic propagation.
- [x] Add a workflow-policy regression proving every compile-heavy PR workflow
      has the intended concurrency group and wraps all measured phases.
- [x] Validate any benchmark manifest/result fixture through the repository
      validators.

## Docs
- [x] Document the telemetry identity, measurement conditions, cold/warm
      distinction, artifact location, and baseline-comparison rule in
      `docs/benchmarking/ci-policy.md` or the nearest canonical CI performance
      document.
- [x] Update the CI workflow documentation with stale-run cancellation
      semantics.
- [x] Regenerate `tasks/SESSION-BRIEF.md` when retiring this task.

## Acceptance criteria
- [x] Every compile-heavy required gate publishes machine-readable phase
      timings and the contextual counters needed for a comparable result.
- [x] A baseline report contains at least five comparable samples per gate and
      reports median/p95 without presenting the representative table as a
      performance claim.
- [x] Pushing a newer commit to one PR cancels older in-progress runs of the
      same workflow without cancelling another PR or the default branch.
- [x] No build tree, BMI, or object cache is introduced.

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

Slice A verification run on 2026-07-09:

```bash
python3 tests/regression/tooling/Test.CiTiming.py -v
python3 tests/regression/tooling/Test_BenchmarkManifestValidator.py -v
python3 tests/regression/tooling/Test_BenchmarkResultValidator.py -v
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 -m py_compile tools/ci/time_command.py tools/ci/aggregate_gate_timing.py tests/regression/tooling/Test.CiTiming.py
git diff --check
```

Slice B verification run on 2026-07-09:

```bash
python3 tests/regression/tooling/Test.CiTiming.py -v
python3 tests/regression/tooling/Test.WorkflowConcurrency.py -v
python3 -m py_compile tools/ci/time_command.py tools/ci/aggregate_gate_timing.py tests/regression/tooling/Test.CiTiming.py tests/regression/tooling/Test.WorkflowConcurrency.py
ctest --test-dir build/ci --show-only=json-v1 -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine'
ninja -C build/ci -t commands IntrinsicTests
python3 -c 'from pathlib import Path; import yaml; [yaml.safe_load(path.read_text()) for path in Path(".github/workflows").glob("*.yml")]'
git diff --check
```

Slice C verification run on 2026-07-09:

```bash
python3 tools/ci/validate_gate_timing_baseline.py
python3 tests/regression/tooling/Test.CiTiming.py -v
python3 tools/benchmark/validate_benchmark_results.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 -m py_compile tools/ci/validate_gate_timing_baseline.py tests/regression/tooling/Test.CiTiming.py
git diff --check
```

The retained source records were also checked through authenticated GitHub API
job/run endpoints: 30/30 jobs and 25/25 distinct workflow runs matched their
workflow IDs, run/job associations, SHAs, events, conclusions, runner images,
phase/job durations, and vcpkg cache-step completion.

## Forbidden changes
- Claiming a percentage improvement without a named comparable baseline.
- Combining cold and warm-cache samples in one statistic.
- Optimizing configure first when build remains the dominant phase.
- Caching build directories, compiler BMIs, or Ninja dependency state.
- Weakening or skipping any current correctness gate.

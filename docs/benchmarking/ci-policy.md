# Benchmark CI Policy

Benchmark execution is split by cost so CI remains fast while still guarding quality.

## PR-fast expectations

- Run only smoke benchmarks.
- Enforce strict runtime budget.
- Validate benchmark result JSON schema.

## Nightly/deep expectations

- Run deeper correctness/performance suites.
- Include broader datasets and optional GPU workloads.
- Publish benchmark artifacts for trend tracking.

## Gate latency telemetry

Compile-heavy workflows emit one result for
`ci.gate-latency.github-ubuntu-24.04.v1`. The result measures configure, build,
and test/execution phases and reports their sum as `total_time_ms`; it does not
include runner queue time, dependency installation, unrelated structural
checks, or artifact upload.

The result uses the canonical benchmark JSON schema with
`backend: external_baseline`. Gate identity, preset, compiler, sanitizer,
runner image, cache state, selected test count, Ninja command-edge count, and
cache diagnostics remain explicit. Unavailable counters are represented by
zero plus a matching `*_available: false` diagnostic rather than being silently
invented.

Cold and warm-cache samples are reported separately. Performance claims require
at least five comparable samples and cite median and p95 plus the exact commit,
runner image, preset, sanitizer, and gate selector.

The CI-003 pre-optimization baseline is
[`benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json`](../../benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json).
It contains five GitHub Actions API samples for each required gate and both
sanitizer matrix legs. Median and p95 use the nearest-rank method; with five
samples p95 is the observed maximum. All baseline compiles are cold because
ccache was absent, while the vcpkg binary-package cache was warm. No warm-
compile population existed, and the baseline records that absence rather than
combining cache states.

Every population uses the same five pull-request commits. The retained run/job
IDs were checked against all 30 jobs and 25 workflow runs through the
authenticated GitHub API; workflow identity, SHA, event, conclusion, runner
image, phase/job durations, and vcpkg cache-step completion matched the
artifact. Compare future cohorts by median/p95 rather than a single run because
the sanitizer samples retain substantial cold-build variance.

The aggregate uses the distinct benchmark ID
`ci.gate-latency.github-ubuntu-24.04.v1.aggregate-baseline` and links to the
per-run `ci.gate-latency.github-ubuntu-24.04.v1` profile through
`diagnostics.source_benchmark_id`. It reports population/sample counts and
grouped cold-population statistics; it does not impersonate the direct
configure/build/test/total metrics emitted by one gate invocation.

The sampled Vulkan test phase predates the completed Xvfb split and failed
before frame-pacing capture, so its test/total timing is diagnostic only. Use
its configure/build baseline until five post-BUG-064 test samples accumulate.

The result artifact is uploaded as `ci-gate-timing-<gate>` (with the sanitizer
name appended for matrix legs) and contains one canonical `result.json`.
Configure, build, and test/execution phase inputs remain job-local and are not
uploaded. If a measured phase fails or never starts, aggregation still emits an
`error`/`failed` result with phase diagnostics and fails closed.

## Stale-run cancellation

Compile-heavy pull-request workflows use this workflow-level concurrency key:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: true
```

A newer commit cancels only an older run of the same workflow for the same pull
request. Different pull requests and default-branch runs have distinct groups.
Manual/push events fall back to the full Git ref. The `ci-linux-clang`
default-branch trigger disables cancellation explicitly so every merged commit
retains its full confidence result and timing artifact.

## Failure policy

- Schema validation failures are hard failures.
- Missing benchmark result roots are reported as blocked prerequisite failures;
  run the benchmark producer target before treating validation as actionable.
- Smoke threshold regressions fail PR-fast once enabled in strict mode.
- Heavy-suite regressions are triaged with explicit follow-up tasks.

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
ccache was absent, while the vcpkg binary-package cache was warm. The `pr-fast`
ccache policy persists only ccache's external content store, reports hit/miss,
cache-size, error, and availability diagnostics, and treats both exact and
compatible-prefix restores as warm samples. Each immutable saved key includes
the configured compiler/scanner, pinned ccache, preset, sanitizer identity,
toolchain/dependency-input hash, and commit SHA. Ccache 4.9.1 passes `.cppm`
compiles through; cacheable C++ consumers run with direct and depend modes off
and hash a deterministic digest of every repository module interface through
`CCACHE_EXTRAFILES`. No warm-compile population existed in the CI-003 baseline,
and the baseline records that absence rather than combining cache states.

Each CI-003 baseline population uses the same five pull-request commits. The
retained run/job IDs were checked against all 30 jobs and 25 workflow runs
through the authenticated GitHub API; workflow identity, SHA, event,
conclusion, runner image, phase/job durations, and vcpkg cache-step completion
matched the artifact. Compare future cohorts by median/p95 rather than a single
run because the sanitizer samples retain substantial cold-build variance.

Before the measured build, `pr-fast` runs a hermetic named-module invalidation
probe with the compiler and `clang-scan-deps` selected by the configured `ci`
preset. The probe uses its own ccache config and content store, keeps the module
implementation and importer byte-for-byte and mtime-stable, and requires an
empty-cache miss, unchanged-source hits, misses after an interface-only layout
change, Ninja importer invalidation, and output parity with a clean no-ccache
build. Its JSON is uploaded as `ci-ccache-module-invalidation-pr-fast`; probe
time and probe-cache statistics are not included in the measured gate result.

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

### CI-007 retained ccache evidence

`CI-007` retained the ccache policy in `pr-fast` only after a contemporary
five-sample cold/warm comparison on commit `5394e51b`. Every sample used
`ubuntu-24.04`, Clang 20, the `ci` preset with project-default ASan/UBSan,
3,549 selected tests, 1,955 Ninja command edges, and an exact vcpkg cache hit.
Every gate and hermetic invalidation probe passed with zero ccache errors.

| Population | Runs | Ccache result per run | Build median / p95 | Total median / p95 |
| --- | --- | --- | ---: | ---: |
| Cold | [29113978973](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29113978973), [29115473396](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29115473396), [29117419922](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29117419922), [29119188858](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29119188858), [29127596925](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29127596925) | 0 hits / 575 misses | 1,406.634 s / 1,510.259 s | 1,634.116 s / 1,753.939 s |
| Warm | [29208730070](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29208730070), [29209234114](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29209234114), [29209736454](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29209736454), [29210235661](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29210235661), [29210606175](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29210606175) | 575 hits / 0 misses | 609.476 s / 630.449 s | 856.085 s / 877.683 s |

The warm population reduced build median by 56.7% and build p95 by 58.3%; total
median and p95 fell by 47.6% and 50.0%. The retained policy is therefore the
bounded `pr-fast` store described above. No other gate consumes it, and any
future expansion requires its own comparable correctness and timing evidence.

The separate repository-interface sample used evidence-only commit `4befbe1e`,
which added one static exported API revision to `Extrinsic.Core.Error` without
changing object layout or importer sources. Hosted run
[29211278659](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29211278659)
restored the compatible prior store, then reported 0 hits, 575 misses, and zero
errors before passing all 3,549 selected tests. The matching clean local
no-ccache source state passed the complete 3,617-test CPU gate. The temporary
API marker was removed by `fd97d4d1`; it was never merged into the production
source surface.

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

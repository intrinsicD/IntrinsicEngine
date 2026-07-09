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

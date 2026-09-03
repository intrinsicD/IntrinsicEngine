# Benchmarks

This directory owns IntrinsicEngine benchmark infrastructure and benchmark artifacts.

## Benchmark categories

- **Smoke**: fast sanity checks suitable for pull-request CI.
- **Correctness**: validates method outputs and error bounds against references.
- **Performance**: CPU/GPU runtime and memory measurements.
- **GPU**: backend-specific benchmark runs requiring graphics/compute support.
- **Nightly**: deeper, slower suites executed outside fast PR gates.

## Layout

- `geometry/`: geometry-processing benchmark suites and manifests.
- `core/`: Core scheduler and task-system benchmark suites and manifests.
- `ci/`: GitHub-hosted CI gate latency contracts and manifests.
- `physics/`: physics and dynamics benchmark suites and manifests.
- `rendering/`: rendering and frame-graph benchmark suites.
- `datasets/`: dataset policy docs and manifests (no large binaries in-repo).
- `baselines/`: benchmark baseline snapshots for regression comparisons.
- `reports/`: generated benchmark reports and summaries.
- `runners/`: benchmark runner binaries and orchestration helpers.

Canonical JSON results use schema v2: stable benchmark identity is separate
from append-only run/attempt identity, and every result binds the exact
manifest, resolved params/warmup/thresholds, source state, and recomputed gate
disposition. Checked-in examples and baselines are non-claim-eligible unless a
claim-grade protocol says otherwise.

## Build integration

- General benchmark scaffolding is wired through `benchmarks/CMakeLists.txt`;
  the opt-in curvature-patch profiler target is declared in
  `benchmarks/curvature_patch/CMakeLists.txt`.
- Native smoke producers run through
  `tools/benchmark/run_and_seal.py`; raw output is sealed before strict
  validation or artifact publication.
- The optimized `ci-release` lane owns the 25-result monolithic
  `IntrinsicBenchmarkSmoke` population, including the Core scheduler probe
  and the two TaskGraph replay-lifecycle workloads.
- The default configuration keeps benchmark binaries lightweight so existing builds remain stable.
- Heavy datasets and long-running suites are deferred to dedicated benchmark tasks.

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
- `rendering/`: rendering and frame-graph benchmark suites.
- `datasets/`: dataset policy docs and manifests (no large binaries in-repo).
- `baselines/`: benchmark baseline snapshots for regression comparisons.
- `reports/`: generated benchmark reports and summaries.
- `runners/`: benchmark runner binaries and orchestration helpers.

## Build integration

- Benchmark scaffolding is wired through `benchmarks/CMakeLists.txt`.
- The default configuration keeps benchmark binaries lightweight so existing builds remain stable.
- Heavy datasets and long-running suites are deferred to dedicated benchmark tasks.

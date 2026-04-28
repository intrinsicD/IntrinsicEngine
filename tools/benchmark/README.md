# tools/benchmark

Benchmark manifest, result, and performance regression tooling.

## Current state

- `tools/benchmark/check_perf_regression.sh` — threshold-based performance regression gate for benchmark JSON outputs.
- `tools/benchmark/validate_benchmark_manifests.py` — benchmark manifest validator.
- `tools/benchmark/validate_benchmark_results.py` — benchmark result JSON validator.

## Notes

- `check_perf_regression.sh` was moved from `tools/check_perf_regression.sh` in RORG-073.

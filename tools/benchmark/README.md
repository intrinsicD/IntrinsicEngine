# tools/benchmark

Benchmark manifest, result, and performance regression tooling.

## Current state

- `tools/benchmark/check_perf_regression.py` — schema-aware frame-performance
  comparison; the `.sh` file is a compatibility exec wrapper.
- `tools/benchmark/validate_benchmark_manifests.py` — benchmark manifest validator.
- `tools/benchmark/validate_benchmark_results.py` — strict schema-v2 result
  validator with exact manifest/source/threshold binding.
- `tools/benchmark/seal_benchmark_results.py` — converts raw producer output
  into non-claim-eligible canonical schema-v2 results unless explicit clean or
  snapshot claim custody is supplied.
- `tools/benchmark/run_and_seal.py` — executes a native producer and seals all
  emitted results before validation.

## Notes

- `check_perf_regression.sh` was moved from `tools/check_perf_regression.sh` in RORG-073.
- Stable `benchmark_id` identifies the definition. Append-only `(run_id,
  attempt_id)` identifies an execution/attempt; official evidence uses a new
  run root rather than overwriting prior failures.

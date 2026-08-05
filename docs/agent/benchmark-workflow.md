# Benchmark Workflow

This document defines how to create and review benchmark work.

## Benchmark lifecycle

1. Define benchmark intent (`smoke`, `correctness`, `performance`, `gpu`, `nightly`).
2. Add/extend benchmark manifest with stable `benchmark_id` and dataset reference.
3. Implement or wire runner.
4. Emit raw metrics + diagnostics, then seal canonical result schema v2
   against the exact manifest/source/config.
5. Validate recomputed threshold disposition and retain the distinct
   run/attempt identity.
6. Compare to a matched baseline and document deltas.

## Manifest requirements

- Stable benchmark IDs.
- Declared method, dataset, params, and metrics.
- Thresholds for smoke checks when applicable.
- Threshold names map one declared metric through `_max` or `_min`.

## Runner requirements

- Deterministic invocation path.
- Distinguish PR-fast smoke from heavy/nightly workloads.
- Avoid requiring external large datasets for smoke checks.
- Native producers use `tools/benchmark/run_and_seal.py`; raw output is not a
  publishable result.
- Claim-grade native runners return zero only for `passed`; `skipped`,
  `failed`, and `error` retain raw diagnostics but return nonzero. Claim-grade
  GPU runners report the requested backend separately from the actual backend
  and use an explicit non-execution state when no request reached that backend.
- Local or unverified source defaults to non-claim-eligible.

## Reporting requirements

- JSON outputs are machine-readable.
- Stable `benchmark_id` is separate from append-only `run_id` and
  `attempt_id`; retries never overwrite failed attempts.
- Duplicate keys, non-finite values, boolean metrics, manifest drift,
  undeclared metrics, and status/gate mismatches fail strict validation.
- Performance claims include baseline comparison.
- Numerical quality/error metrics are included where relevant.
- Claim-grade results additionally use the frozen protocol, portable bundle,
  independent audit, and ARA ledger; a passing result alone authorizes no
  claim.
- Benchmark-backed positive bundle audit requires both canonical
  `execution_status` and recomputed `status` to be `passed`. Non-passed results
  may remain in a portable bundle as negative evidence, but its audit is
  rejected and it cannot satisfy workflow completion.

## Canonical commands

```bash
python3 tools/benchmark/validate_benchmark_manifests.py \
  --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py \
  --root <result-root> --manifests-root benchmarks --strict
```


## Related documentation

- [Benchmarking docs index](../benchmarking/index.md)
- [Benchmark manifest schema](../benchmarking/benchmark-manifest-schema.md)
- [Benchmark result JSON schema](../benchmarking/result-json-schema.md)
- [Benchmark report template](../benchmarking/report-template.md)


## Review checklist

- [Benchmark review checklist](benchmark-review-checklist.md)
- [Workflow evidence and experiment custody](workflow-evidence.md)

# Benchmark Workflow

This document defines how to create and review benchmark work.

## Benchmark lifecycle

1. Define benchmark intent (`smoke`, `correctness`, `performance`, `gpu`, `nightly`).
2. Add/extend benchmark manifest with stable `benchmark_id` and dataset reference.
3. Implement or wire runner.
4. Emit result JSON with metrics + diagnostics.
5. Compare to baseline and document deltas.

## Manifest requirements

- Stable benchmark IDs.
- Declared method, dataset, params, and metrics.
- Thresholds for smoke checks when applicable.

## Runner requirements

- Deterministic invocation path.
- Distinguish PR-fast smoke from heavy/nightly workloads.
- Avoid requiring external large datasets for smoke checks.

## Reporting requirements

- JSON outputs are machine-readable.
- Performance claims include baseline comparison.
- Numerical quality/error metrics are included where relevant.

# Benchmarking Overview

Benchmarking in IntrinsicEngine exists to validate both **correctness** and **performance** with reproducible machine-readable results.

## Benchmark classes

- **Smoke:** Fast, deterministic checks suitable for pull-request validation.
- **Correctness:** Validates numerical output quality against expected references.
- **Performance:** Measures runtime/memory behavior with controlled workloads.
- **GPU:** Backend-specific runs (for example Vulkan) with explicit capability gates.
- **Nightly/deep:** Extended suites outside fast PR loops.

## Core principles

1. Every benchmark uses a stable `benchmark_id`.
2. Every benchmark declares method, dataset, metrics, and thresholds where relevant.
3. Every execution emits schema-valid JSON results.
4. Performance claims must reference a baseline comparison.
5. Heavy workloads must be isolated from PR-fast CI.

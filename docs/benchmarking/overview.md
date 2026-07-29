# Benchmarking Overview

Benchmarking in IntrinsicEngine exists to validate both **correctness** and **performance** with reproducible machine-readable results.

## Benchmark classes

- **Smoke:** Fast, deterministic checks suitable for pull-request validation.
- **Correctness:** Validates numerical output quality against expected references.
- **Performance:** Measures runtime/memory behavior with controlled workloads.
- **GPU:** Backend-specific runs (for example Vulkan) with explicit capability gates.
- **Nightly/deep:** Extended suites outside fast PR loops.

## Core principles

1. Every definition uses a stable `benchmark_id`; executions use distinct,
   append-only `run_id`/`attempt_id`.
2. Every benchmark declares method, dataset, metrics, and thresholds where relevant.
3. Every execution is sealed as schema-v2 JSON against the exact manifest,
   source state, resolved params/warmup, and recomputed thresholds.
4. Performance claims must reference a baseline comparison.
5. Heavy workloads must be isolated from PR-fast CI.
6. Claim eligibility is explicit; local, dirty, historical, and unverified
   results remain diagnostic evidence only.

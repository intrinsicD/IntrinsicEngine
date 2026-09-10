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

## Framed projection neighborhood smoke

The `geometry.point_lbvh.{wlop,wlop_anisotropic,clop,ear}_vulkan_runtime_smoke`
manifests use `IntrinsicRuntimePointCloudConsolidationGpuParityTests`.
After building that target with `ci-vulkan`, set
`INTRINSIC_PROJECTION_LBVH_BENCHMARK_DIR` to an existing empty output directory
and run CTest with `-L gpu -L vulkan -R
'PointCloudConsolidationGpuParity.VulkanLbvh(Wlop|AnisotropicWlop|Clop|Ear)'`.
Each case emits one raw JSON file; seal the directory with
`tools/benchmark/seal_benchmark_results.py` and validate it as described in the
[result schema](result-json-schema.md#commands).

Each warm measurement covers eight canonical domains with 32 samples and three
moving iterations after one cold run, explicitly priming current source storage
after publication. CPU-reference position/normal errors also cover failure and
cardinality cases outside the timed population. Normal error is a diagnostic with
a hard test limit of 1e-6; isotropic methods have no normal output and report zero.
The fixtures exercise paginated complete
rows, capacity rejection, stale/cancelled work and history; EAR includes insertion
and anisotropic methods include an additional estimated-normal request.
These are opt-in correctness smoke timings, not a grid comparison or speedup.

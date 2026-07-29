# Benchmark Manifest Schema

This document defines the canonical schema for benchmark manifests under `benchmarks/**`.

## Purpose

Benchmark manifests provide machine-checkable declarations for benchmark IDs, method bindings, datasets, metrics, and smoke thresholds.

Use these manifests to ensure benchmark runs are reproducible and CI-checkable.

## File locations

- Recommended root: `benchmarks/manifests/` for global manifests.
- Package-local manifests are also valid under benchmark packages (for example `benchmarks/geometry/**`).

## Required fields

Each manifest **must** contain the following top-level fields:

- `benchmark_id` (string)
- `method` (string)
- `dataset` (string)
- `params` (mapping/object)
- `metrics` (non-empty list of strings)
- `thresholds` (mapping/object)

The canonical result copies and hashes the exact resolved manifest. Changing
params, metrics, warmup, or thresholds therefore invalidates an old result's
binding rather than silently reinterpreting it.

## ID and naming rules

- `benchmark_id` must be a non-empty string using dotted namespace style.
- Duplicate `benchmark_id` values are invalid across all scanned manifests.
- `method` is expected to be a method ID (for example `geometry.example`).
- `dataset` should use stable dataset IDs (for example `builtin.triangle_mesh.small`).

## Allowed metrics

The validator currently accepts the following metric names:

- `runtime_ms`
- `memory_peak_bytes`
- `quality_error_l2`
- `quality_error_linf`
- `throughput_items_per_sec`
- `gpu_time_ms`
- `configure_time_ms`
- `build_time_ms`
- `test_time_ms`
- `total_time_ms`
- `population_count`
- `sample_count`
- `warm_population_count`
- `cold_population_statistics`
- `avgFrameTimeMs`
- `p99FrameTimeMs`
- `avgFPS`
- `totalFrames`

Additional metrics can be added in a dedicated schema update task.

## Threshold naming

Each numeric gate ends in `_max` or `_min` and contains exactly one declared
metric name. Examples:

- `smoke_runtime_ms_max` maps to `runtime_ms <= limit`.
- `throughput_items_per_sec_min` maps to
  `throughput_items_per_sec >= limit`.
- `priority_inversion_quality_error_l2_max` maps to
  `quality_error_l2 <= limit`.

The result sealer computes the disposition; producers do not choose a passed
status independently of these gates.

## Placeholders

During incremental migration, these placeholder prefixes are allowed in selected fields:

- `TODO:`
- `TBD`
- `PLACEHOLDER:`

Placeholders are accepted in `method` and `dataset` values to support staged rollout.

## Example

```yaml
benchmark_id: geometry.example.small
method: geometry.example
dataset: builtin.triangle_mesh.small
params:
  intent: smoke
  warmup_iterations: 1
  measured_iterations: 8
metrics:
  - runtime_ms
  - memory_peak_bytes
  - quality_error_l2
thresholds:
  smoke_runtime_ms_max: 200
```

## Validation command

```bash
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks
```

Use strict mode in CI once manifests are broadly adopted:

```bash
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
```

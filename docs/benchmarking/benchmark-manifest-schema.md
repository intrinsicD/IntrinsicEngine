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

Additional metrics can be added in a dedicated schema update task.

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
params: {}
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

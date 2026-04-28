# Benchmark Result JSON Schema

This document defines the canonical JSON payload emitted by IntrinsicEngine benchmark runners.
It is the required interchange format for local runs, CI artifacts, and report ingestion.

## Required top-level fields

Every benchmark result JSON object **must** include:

- `benchmark_id` (string): Stable benchmark identifier, e.g. `geometry.example.small`.
- `method` (string): Method identifier used by the run, e.g. `geometry.example`.
- `backend` (string): Executing backend, e.g. `cpu_reference`, `cpu_optimized`, `gpu_vulkan_compute`.
- `dataset` (string): Dataset identifier, e.g. `builtin.triangle_mesh.small`.
- `commit` (string): Source revision identifier (typically a git SHA).
- `metrics` (object): Key/value map of measured metrics.
- `diagnostics` (object): Key/value map of non-metric runtime diagnostics.
- `status` (string): Run status.

## Status values

Allowed `status` values:

- `passed`
- `failed`
- `skipped`
- `error`

## Recommended optional fields

These fields are optional but recommended for reportability and reproducibility:

- `timestamp_utc` (string): ISO-8601 timestamp.
- `runner` (string): Runner ID or workflow name.
- `host` (string): Machine/runner identifier.
- `notes` (string): Short additional context.

## Metric value constraints

Within `metrics`:

- values should be numeric (`int` or `float`) where possible.
- boolean flags are allowed for explicit capability markers.
- nested objects are allowed only for grouped metric families.

## Example

```json
{
  "benchmark_id": "geometry.example.small",
  "method": "geometry.example",
  "backend": "cpu_reference",
  "dataset": "builtin.triangle_mesh.small",
  "commit": "abcdef1234567890",
  "metrics": {
    "runtime_ms": 1.25,
    "memory_peak_bytes": 24576,
    "quality_error_l2": 0.0
  },
  "diagnostics": {
    "iterations": 12,
    "warmup_runs": 1
  },
  "status": "passed"
}
```

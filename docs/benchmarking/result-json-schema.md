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

## CI gate timing profile

The stable `ci.gate-latency.github-ubuntu-24.04.v1` profile uses
`backend: external_baseline`, method `ci.gate-latency`, and dataset
`github.hosted.ubuntu_24_04.x86_64`. Its metrics are
`configure_time_ms`, `build_time_ms`, `test_time_ms`, and `total_time_ms`.
The measured total is the sum of those phases, not whole-job time.

Gate, preset, compiler, sanitizer, runner image, cold/warm cache state, selected
test count, Ninja command-edge count, ccache hit/miss counts, ccache cache size,
ccache error count, vcpkg cache state, phase return codes, and
unavailable-counter flags belong in `diagnostics`. Cold and warm results are
different populations for baseline comparison.

When a gate requires ccache telemetry, `ccache_stats_required` is true and
`ccache_stats_health` is `healthy`, `invalid`, or `errors_reported`.
Missing/malformed required stats produce result status `error`; a valid payload
with a nonzero ccache error count produces status `failed`. Optional gates use
`not_requested` with `ccache_stats_available=false` rather than fabricating
zero-valued available counters.

The multi-run baseline report uses the distinct ID
`ci.gate-latency.github-ubuntu-24.04.v1.aggregate-baseline`. It links back to
the per-run profile through `diagnostics.source_benchmark_id` and reports
population/sample counts plus grouped cold-population statistics. Consumers
must not interpret those aggregates as one gate invocation.

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

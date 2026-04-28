# Benchmark Metrics

Benchmark metrics must be explicit and suitable for automated regression checks.

## Required metric classes

- **Runtime:** e.g. `runtime_ms`
- **Memory:** e.g. `memory_peak_bytes`
- **Quality/error:** e.g. `quality_error_l2`

## Metric policy

- Prefer stable scalar metrics with clear units.
- Include at least one quality/error metric when algorithmically meaningful.
- Avoid ambiguous names; use canonical metric identifiers recognized by validators.
- If metric semantics change, update schema docs and baselines in the same PR.

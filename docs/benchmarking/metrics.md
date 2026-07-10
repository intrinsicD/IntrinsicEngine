# Benchmark Metrics

Benchmark metrics must be explicit and suitable for automated regression checks.

## Required metric classes

- **Runtime:** e.g. `runtime_ms`
- **Memory:** e.g. `memory_peak_bytes`
- **Quality/error:** e.g. `quality_error_l2`
- **CI gate phases:** `configure_time_ms`, `build_time_ms`, `test_time_ms`,
  `total_time_ms`
- **Aggregate baseline reports:** `population_count`, `sample_count`,
  `warm_population_count`, `cold_population_statistics`

## Metric policy

- Prefer stable scalar metrics with clear units.
- Include at least one quality/error metric when algorithmically meaningful.
- Avoid ambiguous names; use canonical metric identifiers recognized by validators.
- If metric semantics change, update schema docs and baselines in the same PR.

CI gate latency has no algorithmic output, so a quality/error metric is not
applicable. Its quality signal is unchanged gate selection and pass/fail
diagnostics; optimization tasks must compare those alongside timing.
Aggregate reports use a distinct benchmark identity from their source per-run
profile and carry the source ID in diagnostics.

## Comparative smoke timing

When a smoke benchmark gates on a runtime ratio between a baseline and probe,
it must time the two methods individually in interleaved pairs instead of timing
two contiguous blocks. The manifest and result diagnostics must declare the
warmup count, measured-pair count, order policy, and robust statistic used for
the gate. Raw samples remain in diagnostics so ignored outliers are auditable.

For short PR-fast workloads, prefer an odd paired-sample count and the median of
the paired ratios: a minority of scheduler stalls then cannot flip the result,
while a sustained majority regression still fails the unchanged threshold.
Backend runtime summaries may report their individual medians, but the gated
ratio must be computed from paired samples rather than from a ratio of two
independently aggregated blocks. Do not use retries, threshold inflation, or
outlier deletion to manufacture a passing result.

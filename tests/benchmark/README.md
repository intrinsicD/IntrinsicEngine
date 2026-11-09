# Benchmark Tests

Benchmark tests provide SLO-oriented test gates. Benchmark smoke harnesses and
manifest-backed workloads live under `benchmarks/`.

`IntrinsicMethodExecutionMeasurement` is an opt-in runtime integration measurement
seam. Its source stays here because it composes private runtime services; only its
manifests live under `benchmarks/rendering/manifests/`. The
[fixed campaign procedure](../../docs/benchmarking/method-execution-campaign.md)
documents preparation, technical checks, experiment review and raw-derived reports.
Without the campaign configuration environment variables its test skips.

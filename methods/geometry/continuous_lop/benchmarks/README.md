# Benchmarks

The deterministic built-in CLOP correctness smoke is declared by
`benchmarks/geometry/manifests/continuous_lop_reference_smoke.yaml` and executed
by `IntrinsicBenchmarkSmoke`. It reports surface error, WLOP parity, mixture
resolution and contribution counts, uniformity, outlier displacement,
EM/projection convergence, and the fail-closed status. Runtime is diagnostic;
no performance claim is authorized by this package.

The family-wide exact candidate is evaluated by
`benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`; its CLOP
ratio missed the frozen adoption gate and remains negative evidence.

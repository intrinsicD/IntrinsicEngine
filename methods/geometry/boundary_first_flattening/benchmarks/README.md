# Benchmarks

The `METHOD-023` benchmark slice provides the PR-fast correctness smoke at
`benchmarks/geometry/manifests/boundary_first_flattening_reference_smoke.yaml`
and registers its workload with `IntrinsicBenchmarkSmoke`.

The smoke uses a small generated triangle-disk fixture, declares warmup and
measured iterations, emits `runtime_ms` plus the worst measured RMS conformal
`quality_error_l2`, and reports closure-adjustment diagnostics. Every measured
iteration must pass; the result records the worst quality error and failed
iteration count.

This is a correctness and runtime-budget gate only. It makes no speedup,
backend-adoption, or paper-parity performance claim.

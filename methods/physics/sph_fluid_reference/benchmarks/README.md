# Benchmarks

The PR-fast smoke manifest is
[`benchmarks/physics/manifests/sph_fluid_reference_smoke.yaml`](../../../../benchmarks/physics/manifests/sph_fluid_reference_smoke.yaml).
The runner is wired through `IntrinsicBenchmarkSmoke` and emits validated JSON
with `runtime_ms` and `quality_error_l2` (interior-particle relative density
error of a static uniform grid). The dynamic toy-column drop contributes the
stability diagnostics. No performance claim is made without a baseline.

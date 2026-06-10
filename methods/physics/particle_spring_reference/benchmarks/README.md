# Benchmarks

The PR-fast smoke manifest is
[`benchmarks/physics/manifests/particle_spring_reference_smoke.yaml`](../../../../benchmarks/physics/manifests/particle_spring_reference_smoke.yaml).
The runner is wired through `IntrinsicBenchmarkSmoke` and emits validated JSON
with `runtime_ms` and `quality_error_l2` (exact momentum and center-of-mass
conservation error for the symmetric two-particle workload). No performance
claim is made without a baseline.

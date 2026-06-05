# Benchmarks

The PR-fast smoke manifest is
[`benchmarks/physics/manifests/rigid_body_reference_smoke.yaml`](../../../../benchmarks/physics/manifests/rigid_body_reference_smoke.yaml).
The runner is wired through `IntrinsicBenchmarkSmoke` and emits validated JSON
with `runtime_ms` and `quality_error_l2`. No performance claim is made without a
baseline.

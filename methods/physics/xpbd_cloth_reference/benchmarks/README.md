# Benchmarks

The PR-fast smoke manifest is
[`benchmarks/physics/manifests/xpbd_cloth_reference_smoke.yaml`](../../../../benchmarks/physics/manifests/xpbd_cloth_reference_smoke.yaml).
The runner is wired through `IntrinsicBenchmarkSmoke` and emits validated JSON
with `runtime_ms` and `quality_error_l2` (final max stretch residual of the
pinned hanging patch). No performance claim is made without a baseline.

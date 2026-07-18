# Benchmark Reports

Generated benchmark reports and benchmark result JSON payloads are stored here.

- Canonical result payload schema: `docs/benchmarking/result-json-schema.md`.
- Validation tool: `tools/benchmark/validate_benchmark_results.py`.
- Example payload: `benchmarks/reports/examples/example_smoke_result.json`.
- [`core_scheduler_hardening_CORE-007.md`](core_scheduler_hardening_CORE-007.md)
  records the matched-host scheduler-priority and wait-registry comparison for
  CORE-007.
- [`core_taskgraph_plan_reuse_CORE-008.md`](core_taskgraph_plan_reuse_CORE-008.md)
  records the order-balanced, matched-host exact-replay comparison for
  CORE-008's three-pass ECS-like and nine-pass render-prep-like shapes.

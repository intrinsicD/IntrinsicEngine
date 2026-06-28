# Geometry Benchmarks

Geometry benchmark suites live here. Each smoke benchmark exercises a public
`Geometry::*` API on a small, deterministic, CPU-only workload and emits a
machine-readable result JSON.

## Layout

- `Bench_*.cpp` — translation units that implement individual benchmark
  workloads. The workload entry point is declared in a sibling `Bench.*.hpp`
  header that the runner includes.
- `manifests/` — checked-in manifest YAML files. Each manifest binds a
  `benchmark_id` to a method/dataset/metric contract and declares the smoke
  thresholds the benchmark is expected to honour. Validated by
  `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks`.

The matching runner lives at
[`benchmarks/runners/BenchmarkSmokeRunner.cpp`](../runners/BenchmarkSmokeRunner.cpp).
It links `IntrinsicGeometry` so workloads may use the public Geometry C++23
module surface directly; benchmarks must not import `assets`, `ecs`,
`runtime`, `graphics`, or `platform`.

## Manifest schema

Manifests follow the canonical schema documented in
[`docs/benchmarking/benchmark-manifest-schema.md`](../../docs/benchmarking/benchmark-manifest-schema.md).
Required top-level fields are `benchmark_id`, `method`, `dataset`, `params`,
`metrics`, and `thresholds`. Allowed metric names are listed in the schema
document; the smoke runner currently emits `runtime_ms` and
`quality_error_l2`.

The `benchmark_id` in the manifest must match the value embedded in the
runner output JSON for the same workload. The runner reads stable constants
such as `Intrinsic::Bench::Geometry::kHalfedgeSmokeBenchmarkId` and
`kParameterizationDiagnosticsSmokeBenchmarkId` from
[`Bench.GeometrySmoke.hpp`](Bench.GeometrySmoke.hpp) and
`kSurfaceSamplingSmokeBenchmarkId` from
[`Bench.SurfaceSamplingSmoke.hpp`](Bench.SurfaceSamplingSmoke.hpp); manifests
copy the same strings. `kQualityMetricsSmokeBenchmarkId` from
[`Bench.QualityMetricsSmoke.hpp`](Bench.QualityMetricsSmoke.hpp) binds the
point-cloud quality metrics smoke workload. Keeping the constants in the
headers is the binding mechanism.

## Fixture policy

Smoke benchmarks must:

- run on every PR-fast machine (no GPU, no Vulkan, no GLFW, no large datasets);
- be deterministic in their measured numerical outputs (timing is allowed to
  vary but geometric/quality outputs must be repeatable);
- complete within the manifest's `smoke_runtime_ms_max` budget on a typical
  CI runner.

Larger datasets, slower variants, or GPU workloads belong in a dedicated
heavy/nightly benchmark task and are out of scope for this directory.

## Adding a new geometry smoke benchmark

1. Add a new `Bench.<Name>.hpp` declaring the workload entry point and the
   stable `benchmark_id`/`method`/`dataset` constants.
2. Add the matching `Bench_<Name>.cpp` translation unit and list it in
   [`benchmarks/CMakeLists.txt`](../CMakeLists.txt) under the
   `IntrinsicBenchmarkSmoke` target.
3. Add a manifest under `manifests/<name>.yaml` that mirrors the constants
   from the header.
4. Extend the runner so it calls the new workload and emits its result JSON.
5. Run the verification commands below.

## Running the smoke benchmark

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
cmake --build --preset ci --target IntrinsicBenchmarks
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py    --root build/ci/benchmark --strict
```

The CTest registrations under the default CPU gate (label `benchmark`)
exercise the runner and validate the emitted result JSON as part of the
standard `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine'`
flow.

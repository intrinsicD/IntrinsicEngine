# Geometry Benchmarks

Geometry benchmark suites live here. Default smoke benchmarks exercise public
`Geometry::*` APIs on small, deterministic, CPU-only workloads and emit
machine-readable result JSON. Opt-in GPU benchmarks are kept in dedicated
targets with explicit `gpu;vulkan` labels and do not run in the default CPU
gate.

## Layout

- `Bench_*.cpp` — translation units that implement individual benchmark
  workloads. The workload entry point is declared in a sibling `Bench.*.hpp`
  header that the runner includes.
- `manifests/` — checked-in manifest YAML files. Each manifest binds a
  `benchmark_id` to a method/dataset/metric contract and declares the smoke
  thresholds the benchmark is expected to honour. Validated by
  `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks`.

The default matching runner lives at
[`benchmarks/runners/BenchmarkSmokeRunner.cpp`](../runners/BenchmarkSmokeRunner.cpp).
It links `IntrinsicGeometry` so workloads may use the public Geometry C++23
module surface directly; benchmarks must not import `assets`, `ecs`,
`runtime`, `graphics`, or `platform`.

The GEOM-056 KMeans Vulkan smoke is the explicit exception: it lives in
[`Bench_KMeansGpuVulkanSmoke.cpp`](Bench_KMeansGpuVulkanSmoke.cpp), imports the
runtime/Vulkan stack, and is registered as `IntrinsicKMeansGpuBenchmarkSmoke`
with `benchmark;geometry;runtime;gpu;vulkan` labels. It emits GPU timing,
CPU-reference baseline timing, and parity diagnostics for the same deterministic
fixture without claiming a speedup.

## Manifest schema

Manifests follow the canonical schema documented in
[`docs/benchmarking/benchmark-manifest-schema.md`](../../docs/benchmarking/benchmark-manifest-schema.md).
Required top-level fields are `benchmark_id`, `method`, `dataset`, `params`,
`metrics`, and `thresholds`. Allowed metric names are listed in the schema
document; the smoke runner currently emits `runtime_ms` and
`quality_error_l2`, with selected promotion gates also emitting
`quality_error_linf`.

The `benchmark_id` in the manifest must match the value embedded in the
runner output JSON for the same workload. The runner reads stable constants
such as `Intrinsic::Bench::Geometry::kHalfedgeSmokeBenchmarkId` and
`kParameterizationDiagnosticsSmokeBenchmarkId` from
[`Bench.GeometrySmoke.hpp`](Bench.GeometrySmoke.hpp) and
`kSurfaceSamplingSmokeBenchmarkId` from
[`Bench.SurfaceSamplingSmoke.hpp`](Bench.SurfaceSamplingSmoke.hpp); manifests
copy the same strings. `kQualityMetricsSmokeBenchmarkId` from
[`Bench.QualityMetricsSmoke.hpp`](Bench.QualityMetricsSmoke.hpp) binds the
point-cloud quality metrics smoke workload. `kPointCloudFilteringSmokeBenchmarkId`
from [`Bench.PointCloudFilteringSmoke.hpp`](Bench.PointCloudFilteringSmoke.hpp)
binds the GEOM-016 filtering/outlier-removal workload (voxel downsample plus
statistical and radius outlier removal on a two-cluster + injected-outlier
fixture). `kSignedHeatReferenceSmokeBenchmarkId`
from [`Bench.SignedHeatReferenceSmoke.hpp`](Bench.SignedHeatReferenceSmoke.hpp)
binds the signed heat reference workload. `kUvAtlasSmokeBenchmarkId` from
[`Bench.UvAtlasSmoke.hpp`](Bench.UvAtlasSmoke.hpp) binds the GEOM-057
fast-staged versus xatlas comparison workload; its result diagnostics record
the xatlas baseline runtime, fast-staged probe runtime, chart counts, and
quality deltas with `adoption_claim: false`. The same header also exposes
`kUvAtlasPromotionBenchmarkId`, which binds
`geometry.uv_atlas.fast_staged_promotion.smoke`: the GEOM-057 default-promotion
gate over the built-in planar grid, strip, cube, cylinder, octahedron proxy,
high-valence fan, and disconnected-quad fixtures. Its result diagnostics record
per-fixture runtime ratios, signed quality regressions, fast-path flipped
elements, chart-overlap counts, `promotion_pass`, and `adoption_claim`.
For each fixture it warms one fast/xatlas pair, individually times five pairs
with alternating fast→xatlas / xatlas→fast order, and gates on the median
paired runtime ratio. The result retains all raw backend times and paired ratios
while reporting median backend runtimes; this prevents a minority of scheduler
stalls from changing the strict 1.0 mean / 1.25 per-fixture decision without
hiding sustained regressions (`BUG-080`).
Keeping the constants in the headers is the binding mechanism.

## Fixture policy

Smoke benchmarks must:

- run on every PR-fast machine (no GPU, no Vulkan, no GLFW, no large datasets);
- be deterministic in their measured numerical outputs (timing is allowed to
  vary but geometric/quality outputs must be repeatable);
- complete within the manifest's `smoke_runtime_ms_max` budget on a typical
  CI runner.

Larger datasets, slower variants, or additional GPU workloads belong in a
dedicated heavy/nightly benchmark task and must use the same opt-in labeling
pattern as the KMeans Vulkan smoke.

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

## FA-QEM vs classical-QEM simplification quality (GEOM-014, stub)

Planned quality comparison for the feature-aware QEM metric added by GEOM-014.
This is a **quality-only** comparison — no performance claims — pending the
compiled smoke runner (owned by the `UI-028` executor follow-up).

- Fixtures: the in-repo tessellated-cube and grid-plane fixtures used by
  `tests/unit/geometry/Test_Simplification.cpp` (no external large datasets).
- Metric per fixture, at a fixed target face count, for
  `Metric::ClassicalQEM` vs `Metric::FA_QEM`:
  - max vertex-to-original-surface distance (one-sided Hausdorff proxy),
  - sharp-feature vertices retained (`Result::SharpFeatureVerticesPinned`),
  - collapse-rejection counts (`Result::CollapsesRejected{Topology,Quality}`).
- Expected qualitative result: on feature-rich inputs FA-QEM retains sharp
  corners and reports max-surface-distance no worse than classical at the same
  target, matching `Test_Simplification.FeatureAwareCornerErrorNotWorseThanClassical`.

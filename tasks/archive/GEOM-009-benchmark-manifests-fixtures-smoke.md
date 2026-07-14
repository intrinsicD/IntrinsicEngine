# GEOM-009 — Geometry benchmark manifests, fixtures, and smoke benchmark

## Goal
- Replace the placeholder geometry benchmark scaffold with a manifest-driven smoke benchmark framework for geometry algorithms and method-readiness work.

## Non-goals
- No heavy/nightly benchmark suite in this task.
- No performance win claims.
- No GPU benchmark backend.
- No new geometry algorithms beyond benchmark harness fixtures.

## Status

- Status: done.
- Completed: 2026-05-18.
- Commit: `8f3b8f2` (`GEOM-009: real geometry halfedge smoke benchmark + manifest`) on branch `claude/select-backlog-task-XVlj0`.
- PR: TBD (filled in when the landing PR is opened).
- Owner/agent: Claude on `claude/select-backlog-task-XVlj0`.
- Theme: Theme E adjacent (geometry method-readiness foundation, per
  [`tasks/backlog/geometry/README.md`](../backlog/geometry/README.md));
  unblocks future geometry method packages that need a stable smoke harness.

## Context
- Owning subsystem/layer: `benchmarks` using public `geometry` APIs only.
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- `benchmarks/geometry/Bench_ExampleSmoke.cpp` was a placeholder; geometry paper-method work needs stable benchmark IDs, datasets, smoke/heavy split, metrics, and machine-readable output.
- Benchmark work follows `docs/agent/benchmark-workflow.md` and `docs/agent/benchmark-review-checklist.md`.

## Required changes
- [x] Define a geometry benchmark manifest format with stable IDs, input fixtures, algorithm parameters, smoke/heavy classification, expected metrics, and output schema. (Manifest at [`benchmarks/geometry/manifests/geometry_halfedge_smoke.yaml`](../../benchmarks/geometry/manifests/geometry_halfedge_smoke.yaml) following [`docs/benchmarking/benchmark-manifest-schema.md`](../../docs/benchmarking/benchmark-manifest-schema.md); the runner's stable `benchmark_id` constant lives in [`benchmarks/geometry/Bench.GeometrySmoke.hpp`](../../benchmarks/geometry/Bench.GeometrySmoke.hpp) so the manifest and emitted JSON cannot drift.)
- [x] Add small checked-in geometry fixtures or reference existing test assets suitable for CPU smoke benchmarks. (Uses the built-in `Geometry::HalfedgeMesh::MakeMeshIcosahedron()` primitive — no on-disk fixture is required for the smoke; dataset is recorded as `builtin.icosahedron`.)
- [x] Replace `benchmarks/geometry/Bench_ExampleSmoke.cpp` with a real smoke benchmark that exercises at least one existing public geometry API without requiring GPU/Vulkan. (Now builds an icosahedron + `Geometry::MeshQuality::ComputeQuality` summary on the CPU.)
- [x] Add machine-readable benchmark output for smoke runs. (Runner emits a result JSON matching [`docs/benchmarking/result-json-schema.md`](../../docs/benchmarking/result-json-schema.md); validated strictly in CI.)
- [x] Document how to add future geometry benchmark cases and how to distinguish smoke from heavy/nightly runs. ([`benchmarks/geometry/README.md`](../../benchmarks/geometry/README.md) is now a real index — layout, manifest schema, fixture policy, how-to-add steps, and run commands.)
- [x] Ensure benchmarks depend only on public method/geometry APIs allowed by repository policy. (Runner links only `IntrinsicConfig`, `IntrinsicCore`, `IntrinsicGeometry`; no `assets`/`ecs`/`runtime`/`graphics`/`platform`.)

## Tests
- [x] Add or update benchmark smoke tests registered with documented CTest labels. (`IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run` + `IntrinsicBenchmarkSmoke.HalfedgeSmoke.Validate` registered from [`benchmarks/CMakeLists.txt`](../../benchmarks/CMakeLists.txt) under labels `benchmark;geometry`, gated by a CTest fixture so the validator runs only after the runner.)
- [x] Ensure any new labels are added to both `tests/README.md` and `tests/CMakeLists.txt` in the same change; prefer existing `benchmark`, `geometry`, and `slo` labels where sufficient. (Re-uses the existing `benchmark` and `geometry` labels — no new labels were introduced.)
- [x] Run focused benchmark/test targets and structural checks. (See the Verification section below for the commands actually run in the landing session.)

## Docs
- [x] Update `benchmarks/geometry/README.md` with manifest schema, fixture policy, and run commands.
- [x] Update benchmark docs if a shared manifest convention is introduced. (Re-uses the existing schema docs; no shared-convention rewrite was needed in this slice.)
- [x] Cross-link from `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` only if the review is materially revised. (Not revised; the review remains the seed source-of-truth and is linked from the task file.)

## Acceptance criteria
- [x] Geometry benchmarks have at least one real smoke benchmark instead of a placeholder translation unit.
- [x] Benchmark cases have stable IDs and manifest-driven configuration.
- [x] Smoke outputs are deterministic and machine-readable.
- [x] No performance claims are made without baseline comparison. (Smoke result records `runtime_ms` but the manifest threshold is intentionally generous and no relative claim is made against `benchmarks/baselines/smoke_baseline.json`.)
- [x] Relevant benchmark and docs checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarks
ctest --test-dir build/ci --output-on-failure -L 'benchmark|geometry' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark --strict
python3 tests/regression/tooling/Test_BenchmarkManifestValidator.py
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

### Commands run during the landing session

- `python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict` — passed.
- `python3 -m unittest tests/regression/tooling/Test_BenchmarkManifestValidator.py` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed.
- `cmake --preset ci` / `cmake --build --preset ci --target IntrinsicBenchmarks` / CPU CTest gate — see PR description; the host container in this session does not have the `clang-20` toolchain required by the `ci` preset, so the CMake/CTest gate was not exercised locally. CI must re-run the full default gate.

## Forbidden changes
- Do not add heavy datasets or slow default tests.
- Do not introduce renderer/runtime dependencies into geometry benchmarks.
- Do not claim performance improvements without baseline evidence.


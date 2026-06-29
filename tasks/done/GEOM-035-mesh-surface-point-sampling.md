---
id: GEOM-035
theme: none
depends_on: []
completed_on: 2026-06-28
---
# GEOM-035 — Triangle-mesh surface point sampling

## Goal
- Add a deterministic `src/geometry` API that samples points on a triangle mesh surface (area-weighted barycentric sampling) and returns a populated `Geometry::PointCloud::Cloud`, so meshes (e.g. Stanford bunny/dragon/Lucy) can be turned into point clouds for downstream sampling/analysis methods.

## Non-goals
- No Poisson-disk or blue-noise sampling here; this produces a dense uniform-on-surface cloud only (the input to METHOD-012, not a replacement for it).
- No GPU backend.
- No renderer/runtime/ECS/assets/app integration.
- No mesh repair, remeshing, or normal re-estimation beyond interpolating existing per-vertex normals.

## Context
- Status: done. Completed 2026-06-28 at `CPUContracted`.
- Commit reference: this retirement commit.
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus `benchmarks/geometry` using public geometry APIs only.
- Today the engine has `Geometry::HalfedgeMesh::Mesh` with triangle views (`Geometry.HalfedgeMesh.Utils` `TryGetTriangleFaceView`, `TriangleArea`) and `Geometry::PointCloud::Cloud` (`Geometry.PointCloud`), but there is **no** routine to sample points uniformly on triangle surfaces — only full-mesh vertex conversion (`Geometry.PointCloud.Conversion`, `Geometry.Mesh.Conversion`). The GPU Poisson method needs a dense surface cloud as input, so this is the missing mesh→cloud preprocessing primitive.
- Follow `GEOM-005` API/numeric policy and `GEOM-007` robust-predicate/tolerance policy for degenerate triangles; mirror the diagnostics/determinism style of `GEOM-016`.

## Required changes
- [x] Add a surface-sampling API (new partition `Geometry.PointCloud.SurfaceSampling` or an addition to `Geometry.PointCloud.Conversion`, splitting only if the implementation unit exceeds the `AGENTS.md` §5 interface-hygiene bar) that takes a `HalfedgeMesh::Mesh` (or its triangle view) plus a target sample count and a seed, and returns a `PointCloud::Cloud`.
- [x] Implement area-weighted face selection (cumulative-area table + binary search) and uniform barycentric sampling (`u,v` with the `sqrt` correction) so density is uniform per unit surface area independent of triangulation.
- [x] Interpolate per-vertex normals into `p:normal` when the source mesh carries them; otherwise compute the geometric face normal for each sample.
- [x] Make output fully deterministic for a fixed `(mesh, count, seed)` and report the seed plus accepted/total face counts in a result diagnostics struct.
- [x] Define invalid-input handling for empty meshes, non-triangle faces, zero-area / degenerate triangles, non-finite positions, and zero/negative target counts.

## Tests
- [x] Add `unit;geometry` tests asserting uniform-area density on a known mesh (e.g. a subdivided unit square / two-triangle quad): sample-count proportion per face matches face-area proportion within tolerance.
- [x] Add a determinism test proving identical samples for identical `(mesh, count, seed)` and different output for a different seed.
- [x] Add edge-case tests for empty mesh, degenerate/zero-area triangles, non-triangle faces, and invalid counts.
- [x] Add or update a `benchmark;geometry` smoke manifest/runner under `benchmarks/geometry/` for surface sampling (smoke/quality evidence only, no perf claims), introducing no new CTest labels unless `tests/README.md` and `tests/CMakeLists.txt` are updated in the same change.

## Docs
- [x] Update [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md) (and the point-cloud algorithm roadmap if scope shifts) with the surface-sampling contract and diagnostics.
- [x] Update `benchmarks/geometry/README.md` or manifests if a new benchmark ID is introduced.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] A public geometry API samples a triangle mesh into a `PointCloud::Cloud` with area-uniform density, optional interpolated normals, explicit diagnostics, and deterministic seeded output.
- [x] Degenerate and invalid inputs fail closed with explicit diagnostics rather than asserting or producing NaNs.
- [x] Tests cover area-uniformity, determinism, and edge cases; a CPU-only smoke benchmark exists without performance claims.
- [x] `geometry -> core` layering is preserved and benchmark deps remain limited to public geometry APIs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SurfaceSampling|PointCloud|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed verification:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'SurfaceSampling|PointCloud|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work (no Poisson-disk / blue-noise logic in this task).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted` (a deterministic CPU geometry API with diagnostics and tests).
- No `Operational` follow-up is owed; this task has no GPU/backend seam.

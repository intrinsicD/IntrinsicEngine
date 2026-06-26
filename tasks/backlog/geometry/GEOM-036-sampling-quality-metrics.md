---
id: GEOM-036
theme: none
depends_on: []
---
# GEOM-036 — Blue-noise / sampling quality-metrics module

## Goal
- Add a deterministic `src/geometry` analysis module that computes the blue-noise / Poisson-disk quality metrics a sampling paper needs — radial distribution function (RDF) `g(r)`, radially-averaged power spectrum (RAPS), 2D periodogram, nearest-neighbor distance histogram with coefficient of variation (CV), measured minimum-distance and Poisson-disk ratio, and coverage — as plain CPU functions over a point set, returning numeric arrays suitable for export and plotting.

## Non-goals
- No plotting, rasterization, or image generation here; this produces numeric arrays only (rendering/export is RUNTIME-133 and external scripts).
- No GPU backend.
- No renderer/runtime/ECS/assets/app integration.
- No sampler/method logic; this evaluates an arbitrary point set regardless of how it was produced.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core`; may use geometry-owned `Geometry.KDTree`/`Geometry.Octree` for neighbor queries) plus `benchmarks/geometry` using public geometry APIs only.
- These metrics are the evaluation backbone of the GPU progressive Poisson-disk paper (see the sibling `FIGURES.md` §B: RDF+RAPS, periodograms, NN histograms, min-distance guarantee, CV). The engine has spatial structures (`Geometry.KDTree`, `Geometry.Octree`) and point statistics (`Geometry.PointCloud.Utils`) but **no** RDF/RAPS/periodogram/NN-CV analysis. This module supplies the numbers; METHOD-012/013 produce the samples; RUNTIME-133 exports them for figures.
- Follow `GEOM-005` numeric policy; reuse existing KD-tree/Octree for neighbor queries rather than adding a new acceleration structure.

## Required changes
- [ ] Add an analysis partition (e.g. `Geometry.PointCloud.QualityMetrics`) exposing free functions for: nearest-neighbor distances + CV (std/mean), measured minimum pairwise distance and Poisson-disk ratio `min_dist / r` given a target radius, RDF `g(r)` with edge correction over a configurable bin set, and coverage (fraction of an input set within a radius of the sample).
- [ ] Add spectral metrics for 2D point sets: a periodogram (binned power spectrum of the sampling function) and its radially-averaged 1D profile (RAPS); document the 2D restriction and reject non-2D input with explicit diagnostics.
- [ ] Return results as owned numeric arrays (bin centers + values) plus scalar summaries in a diagnostics struct; make every metric deterministic for a fixed input and parameters.
- [ ] Define invalid-input handling for empty/one-point sets, non-finite coordinates, non-positive radii/bin widths, and dimension mismatches (e.g. RAPS requested on 3D input).

## Tests
- [ ] Add `unit;geometry` tests with analytic baselines: white-noise (Poisson process) RDF approaches 1 away from 0; a jittered-grid / known blue-noise set has CV below a documented threshold; min-distance ratio is `>= 1` for a set known to satisfy a radius.
- [ ] Add a periodogram/RAPS test on a regular lattice (expected spectral peaks at lattice frequencies) and on white noise (flat spectrum within tolerance).
- [ ] Add edge-case tests for empty/one-point sets, non-finite inputs, invalid bins/radii, and RAPS-on-3D rejection.
- [ ] Add or update a `benchmark;geometry` smoke manifest/runner for the metrics pack (smoke/quality evidence only), introducing no new CTest labels unless `tests/README.md` and `tests/CMakeLists.txt` are updated together.

## Docs
- [ ] Document the metric definitions, units, and edge-correction in [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md) (or a dedicated metrics doc) so figure captions can cite exact formulas.
- [ ] Update `benchmarks/geometry/README.md` or manifests if a new benchmark ID is introduced.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] RDF, RAPS, periodogram, NN-distance+CV, min-distance/Poisson ratio, and coverage are available as deterministic CPU functions returning numeric arrays plus scalar summaries.
- [ ] Analytic baselines (white noise, jittered grid, regular lattice) validate each metric within documented tolerances.
- [ ] Invalid inputs and dimension mismatches fail closed with explicit diagnostics.
- [ ] `geometry -> core` layering is preserved and benchmark deps remain limited to public geometry APIs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'QualityMetrics|PointCloud|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Embedding plotting/rasterization or file IO in the metric functions (export is RUNTIME-133).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted` (deterministic CPU analysis API with analytic-baseline tests).
- No `Operational` follow-up is owed; this task has no GPU/backend seam.

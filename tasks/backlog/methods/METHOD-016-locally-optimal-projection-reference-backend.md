---
id: METHOD-016
theme: I
depends_on: [GEOM-062]
maturity_target: CPUContracted
---
# METHOD-016 — Locally Optimal Projection (LOP/WLOP) point-cloud consolidation reference backend

## Goal
- Add a CPU reference method package for parameterization-free point-cloud consolidation via Locally Optimal Projection: project a noisy, outlier-ridden raw cloud onto a cleaner, more uniformly distributed point set without estimating normals or a surface first. This task also establishes the shared `Geometry.PointCloud.Consolidation` surface (strategy + backend axis) that the rest of the LOP family extends.

## Non-goals
- No normal estimation and no surface reconstruction — consolidation only (reconstruction consumers stay in `Geometry.SurfaceReconstruction`).
- No CLOP continuous/GMM variant (owned by `METHOD-017`) and no edge-aware/anisotropic variants such as EAR (owned by `METHOD-018`); this task implements only the `Lop`/`Wlop` strategies, but shapes the strategy axis so those slot in.
- No GPU or optimized backend before reference parity (`METHOD-019` optimized CPU, `METHOD-020` GPU).
- No runtime/config/UI integration — the engine-facing config lane and editor panel are `RUNTIME-175` / `UI-035`.

## Context
- Paper/method: Lipman, Cohen-Or, Levin, Tal-Ezer — "Parameterization-free Projection for Geometry Reconstruction", SIGGRAPH 2007 (LOP); Huang, Li, Zhang, Ascher, Cohen-Or — "Consolidation of Unorganized Point Clouds for Surface Reconstruction", SIGGRAPH Asia 2009 (WLOP).
- Method package: `methods/geometry/locally_optimal_projection/`.
- Port source: framework24 `lib_bcg_framework/include/bcg_locally_optimal_projection.h` (untested in bcg; the density weighting and repulsion terms carry over with explicit tests here).
- Uses `Geometry.KDTree` for neighborhood queries and the existing seeded random subsampling in `Geometry.PointCloud.Utils` for projected-set initialization; uniformity assertions reuse the retired `GEOM-036` sampling-quality metrics.
- Weighting gate: the attraction/repulsion radial weight `θ`, the repulsion function, and the WLOP density weights come from the shared `Geometry.PointCloud.Kernels` seam (`GEOM-062`) rather than private helpers, so CLOP (`METHOD-017`) and EAR (`METHOD-018`) reuse the same tested weight math.
- Foundation for the LOP family: this task introduces `Geometry.PointCloud.Consolidation` with the `Strategy` × `Backend` dispatch surface from `docs/architecture/algorithm-variant-dispatch.md` (the `Geometry.KMeans` exemplar). Follow-ups extend the same module: CLOP (`METHOD-017`), EAR/anisotropic (`METHOD-018`), optimized CPU (`METHOD-019`), GPU (`METHOD-020`), and the engine integration (`RUNTIME-175`/`UI-035`).
- Complements `METHOD-015`: consolidation is the standard preprocessing stage before registration or reconstruction on scanner data.

## Variants and default selection

Mark `[x]` next to the variant that should be the public-facing default backend.

- [x] **A — WLOP (Huang et al. 2009).** Density-weighted attraction plus repulsion; robust on non-uniform raw scans. **Selected as the default.**
- [ ] **B — Plain LOP (Lipman et al. 2007).** Unweighted; available as a variant token (WLOP with unit density weights).

Default recommendation: **A**; B falls out of the same implementation with density weights disabled.

These are the two `Strategy` tokens this task ships. The full state-of-the-art LOP family shares the one `ConsolidationParams::Strategy` axis so a caller/config/UI chooses among all of them through one surface: `Lop`/`Wlop` here, `Clop` (`METHOD-017`), and `Ear` plus anisotropic weighting (`METHOD-018`); the `Backend` axis (`CPU`/`GPU`) is owned by `METHOD-019`/`METHOD-020`.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/locally_optimal_projection/`.
- [ ] Fill `method.yaml` (`id: geometry.locally_optimal_projection`; metrics: `mean_distance_to_reference_surface`, `uniformity_min_pairwise_distance`, `iterations`, `runtime_ms`).
- [ ] Fill `paper.md`.

### Public API in `src/geometry`
- [ ] Add module `Geometry.PointCloud.Consolidation` (`.cppm` + `.cpp`) shaped per the `Strategy` × `Backend` template (`docs/architecture/algorithm-variant-dispatch.md`): a `ConsolidationParams` with a `Strategy` selector (`Lop`/`Wlop` here; extended by follow-ups), a `Backend` request (`CPU`/`GPU`; only `CPU` implemented here), the shared knobs (support radius `h`, repulsion weight `mu` in [0, 0.5), iteration count, target point count or explicit initial indices, seed), and `Consolidate(cloud, params)` returning projected positions plus a convergence `Result` that reports `RequestedBackend`/`ActualBackend`/`FellBackToCPU` and iteration/convergence diagnostics.
- [ ] Consume the shared `Geometry.PointCloud.Kernels` (`GEOM-062`) for the attraction/repulsion weights and WLOP density weights; no private weight math.
- [ ] Deterministic: seeded initialization and fixed iteration order; identical `(seed, input, params)` produce bitwise-identical output across runs and thread counts.
- [ ] Fail-closed on empty or too-small clouds, non-finite positions, `mu` outside [0, 0.5), and non-positive `h`, with explicit failure states.
- [ ] Register the module in `src/geometry/CMakeLists.txt` (single `IntrinsicGeometry` target; alphabetical placement, no new link dependency).

### Benchmarks
- [ ] Smoke benchmark manifest on deterministic synthetic fixtures (noisy plane/sphere with injected outliers) reporting the metrics above; no external datasets.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` with `unit;geometry` labels.
- [ ] Denoising: on noisy plane and sphere fixtures, mean distance to the true surface strictly decreases versus the raw input and falls under a documented bound.
- [ ] Uniformity: repulsion (`mu > 0`) improves the `GEOM-036` min-pairwise-distance metric versus `mu = 0`.
- [ ] Outliers: sparse injected outliers do not pull the projected set beyond tolerance with WLOP density weights engaged.
- [ ] Variant B: a unit-weight run matches the plain-LOP expectations on the plane fixture.
- [ ] Determinism and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/locally_optimal_projection/README.md` with parameter-selection guidance (`h`, `mu`) and known limitations (thin structures, strongly anisotropic sampling).
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Variant A marked default; variant B available from the same implementation.
- [ ] All correctness tests pass in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Public API type discipline: the exported surface uses only `std`/`glm`/scalar types plus the engine's own point-cloud types — the `Cloud`-taking `Consolidate(cloud, params)` entry is explicitly in-contract (vertex `PropertySet` access, matching the `Geometry.PointCloud.Normals` house pattern); no third-party types and no method-internal solver state are exported.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|PointCloud' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without baseline.
- No normal-estimation or reconstruction coupling.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Wlop`/`Lop` reference strategies (correctness-first per the method workflow).
- `Operational` owned by `METHOD-019` (optimized CPU backend) and `METHOD-020` (GPU backend); the CLOP and EAR reference strategies open as `METHOD-017` and `METHOD-018`, and the engine runtime/UI integration as `RUNTIME-175`/`UI-035`.

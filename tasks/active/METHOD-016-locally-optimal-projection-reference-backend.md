---
id: METHOD-016
theme: I
depends_on: []
maturity_target: CPUContracted
---
# METHOD-016 — Locally Optimal Projection (LOP/WLOP) point-cloud consolidation reference backend

## Status

- in-progress; owner: agent session on branch `claude/geometry-processing-research-edptdx`.
- Paper intake runs as real multi-source literature research (deep-research
  harness) before the reference implementation; the digest lands in the method
  package `paper.md` with citations.
- Next verification step: full `ci` preset build + CPU gate in CI on the pushed
  branch (local vcpkg bootstrap is egress-blocked in this environment, BUG-065);
  local verification uses the structural checks plus a standalone clang-20
  numerical sanity harness of the WLOP update math.

## Slice plan

- **Slice A (this task's original scope).** Method package
  `methods/geometry/locally_optimal_projection/`, geometry module
  `Geometry.PointCloud.Consolidation` (CPU reference), correctness tests,
  smoke benchmark manifest. Closes `CPUContracted`.
- **Slice B (session extension, user-requested end-to-end).** Sandbox editor
  integration: new `SandboxEditorGeometryProcessingAlgorithm` entry for the
  point-cloud domain with params panel, execution dispatch, and diagnostics
  readout, following the existing point-cloud algorithm pattern. Editor-model
  coverage stays inside the default CPU gate; no GPU/optimized backend.

## Goal
- Add a CPU reference method package for parameterization-free point-cloud consolidation via Locally Optimal Projection: project a noisy, outlier-ridden raw cloud onto a cleaner, more uniformly distributed point set without estimating normals or a surface first.

## Non-goals
- No normal estimation and no surface reconstruction — consolidation only (reconstruction consumers stay in `Geometry.SurfaceReconstruction`).
- No edge-aware/anisotropic variants (for example EAR) in this package; they open as follow-up variants.
- No GPU or optimized backend before reference parity.

## Context
- Paper/method: Lipman, Cohen-Or, Levin, Tal-Ezer — "Parameterization-free Projection for Geometry Reconstruction", SIGGRAPH 2007 (LOP); Huang, Li, Zhang, Ascher, Cohen-Or — "Consolidation of Unorganized Point Clouds for Surface Reconstruction", SIGGRAPH Asia 2009 (WLOP).
- Method package: `methods/geometry/locally_optimal_projection/`.
- Port source: framework24 `lib_bcg_framework/include/bcg_locally_optimal_projection.h` (untested in bcg; the density weighting and repulsion terms carry over with explicit tests here).
- Uses `Geometry.KDTree` for neighborhood queries and the existing seeded random subsampling in `Geometry.PointCloud.Utils` for projected-set initialization; uniformity assertions reuse the retired `GEOM-036` sampling-quality metrics.
- Complements `METHOD-015`: consolidation is the standard preprocessing stage before registration or reconstruction on scanner data.

## Variants and default selection

Mark `[x]` next to the variant that should be the public-facing default backend.

- [x] **A — WLOP (Huang et al. 2009).** Density-weighted attraction plus repulsion; robust on non-uniform raw scans. **Selected as the default.**
- [ ] **B — Plain LOP (Lipman et al. 2007).** Unweighted; available as a variant token (WLOP with unit density weights).

Default recommendation: **A**; B falls out of the same implementation with density weights disabled.

## Required changes

### Method package scaffolding
- [x] Clone `methods/_template/` to `methods/geometry/locally_optimal_projection/`.
- [x] Fill `method.yaml` (`id: geometry.locally_optimal_projection`; manifest metrics restricted to the validator allowlist `runtime_ms` + `quality_error_l2`, with the domain metrics — mean distance to surface, min pairwise distance, iterations — emitted in the runner's `diagnostics` object, matching every other geometry runner).
- [x] Fill `paper.md` (deep-research digest with verified equations and claims table).

### Public API in `src/geometry`
- [x] Add module `Geometry.PointCloud.Consolidation` (`.cppm` + `.cpp`): `WlopParams` (support radius `h`, repulsion weight `mu` in [0, 0.5), iteration count, target point count or explicit initial indices, seed, variant token) and `Consolidate(cloud, params)` returning projected positions plus a convergence report. Span overload added alongside the `Cloud` overload; the seeded draw restates `Geometry.PointCloud.Utils` `RandomSubsample`'s partial Fisher-Yates in the index domain so the span path needs no `Cloud` copy.
- [x] Deterministic: seeded initialization and fixed iteration order; single-threaded reference, so identical `(seed, input, params)` are bitwise-identical across runs and thread counts.
- [x] Fail-closed on empty or too-small clouds, non-finite positions, `mu` outside [0, 0.5), and non-positive `h`, with explicit failure states (plus invalid iteration/target/initial-index statuses).
- [x] Register the module in `src/geometry/CMakeLists.txt`.

### Benchmarks
- [x] Smoke benchmark manifest on a deterministic noisy-plane fixture reporting `runtime_ms` + `quality_error_l2` with domain metrics in diagnostics; no external datasets. (Sphere and outlier fixtures exercise the correctness tests rather than the smoke benchmark, keeping the smoke fast.)

## Tests
- [x] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` with `unit;geometry` labels.
- [x] Denoising: on noisy plane and sphere fixtures, mean distance to the true surface strictly decreases versus the raw input and falls under a documented bound (plane 0.012 vs ~0.0082 measured; sphere 0.013 vs ~0.0095 measured).
- [x] Uniformity: repulsion (`mu > 0`) improves the `GEOM-036` min-pairwise-distance metric versus `mu = 0`.
- [x] Outliers: sparse injected outliers beyond `h` do not pull the projected set beyond tolerance with explicit on-surface seeds (documented limitation: a seed placed on an isolated outlier stays there).
- [x] Variant B: a unit-weight run meets the plain-LOP expectations on the plane fixture.
- [x] Determinism (across runs and overloads) and fail-closed cases as listed above.

## Docs
- [x] `methods/geometry/locally_optimal_projection/README.md` with parameter-selection guidance (`h`, `mu`) and known limitations (thin structures, strongly anisotropic sampling).
- [x] Regenerate the module inventory.
- [x] Add the method to `docs/methods/index.md`.

## Acceptance criteria
- [x] Variant A marked default; variant B available from the same implementation.
- [ ] All correctness tests pass in the default CPU gate. (Owed to CI: this
  environment cannot bootstrap vcpkg, BUG-065; every threshold is
  pre-verified by a standalone clang-20 harness mirroring the fixtures.)
- [ ] Benchmark smoke manifest validates and runs. (Manifest validates
  strict locally; the run is owed to CI with the same BUG-065 deferral.)
- [x] Public API exposes only `std`/`glm`/scalar types (plus the repo's own `Cloud` handle in the overload, matching every other geometry algorithm module).

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
- Target: `CPUContracted`; optimized backends and edge-aware variants open as follow-up method tasks after reference parity.

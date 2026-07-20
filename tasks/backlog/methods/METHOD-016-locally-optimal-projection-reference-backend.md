---
id: METHOD-016
theme: I
depends_on: [GEOM-062]
maturity_target: CPUContracted
---
# METHOD-016 — Locally Optimal Projection (LOP/WLOP) point-cloud consolidation reference backend

## Goal
- Add a CPU reference method package for parameterization-free point-cloud
  consolidation via LOP/WLOP and establish the shared
  `Geometry.PointCloud.Consolidation` strategy surface. Backend selection is
  added only when `METHOD-019` lands a real second implementation.

## Non-goals
- No normal estimation and no surface reconstruction — consolidation only (reconstruction consumers stay in `Geometry.SurfaceReconstruction`).
- No CLOP continuous/GMM variant (owned by `METHOD-017`) and no edge-aware/anisotropic variants such as EAR (owned by `METHOD-018`); this task implements only the `Lop`/`Wlop` strategies, but shapes the strategy axis so those slot in.
- No GPU or optimized backend before reference parity (`METHOD-019` optimized CPU, `METHOD-020` GPU).
- No runtime/config/UI integration — the engine-facing config lane and editor panel are `RUNTIME-175` / `UI-035`.

## Context
- Paper/method: Lipman, Cohen-Or, Levin, Tal-Ezer — "Parameterization-free Projection for Geometry Reconstruction", SIGGRAPH 2007 (LOP); Huang, Li, Zhang, Ascher, Cohen-Or — "Consolidation of Unorganized Point Clouds for Surface Reconstruction", SIGGRAPH Asia 2009 (WLOP).
- Method package: `methods/geometry/locally_optimal_projection/`.
- Port source: framework24
  `lib_bcg_framework/include/bcg_locally_optimal_projection.h`, used only as
  out-of-build comparison material. Record repository/revision/license
  provenance during intake and implement from the papers; copy no code unless
  its license is explicitly compatible. The bcg path is untested, so density
  and repulsion terms require independent tests here.
- Uses `Geometry.KDTree` for neighborhood queries and the existing seeded random subsampling in `Geometry.PointCloud.Utils` for projected-set initialization; uniformity assertions reuse the retired `GEOM-036` sampling-quality metrics.
- Weighting gate: the attraction/repulsion radial weight `θ`, the repulsion function, and the WLOP density weights come from the shared `Geometry.PointCloud.Kernels` seam (`GEOM-062`) rather than private helpers, so CLOP (`METHOD-017`) and EAR (`METHOD-018`) reuse the same tested weight math.
- Foundation for the LOP family: this task introduces
  `Geometry.PointCloud.Consolidation` with a typed `Strategy` payload. CLOP
  (`METHOD-017`) and EAR (`METHOD-018`) extend that strategy surface;
  `METHOD-019` adds the first justified backend selector when
  `cpu_optimized` exists, and `METHOD-020` owns the runtime GPU adapter.
- Complements `METHOD-015`: consolidation is the standard preprocessing stage before registration or reconstruction on scanner data.

## Variants and default selection

- **Default — WLOP** (Huang et al. 2009), with density-weighted attraction
  and repulsion.
- **Alternative — plain LOP** (Lipman et al. 2007), represented by the same
  typed payload with density weighting disabled.

These are the two `Strategy` alternatives this task ships. The family shares
one typed strategy surface: `Lop`/`Wlop` here, `Clop` in `METHOD-017`, and
`Ear`/anisotropic weighting in `METHOD-018`. No unimplemented backend token is
reserved.

## Slice plan
- **Slice A — intake/contract.** Freeze objective terms, coordinate/radius
  units, initialization, stopping rules, strategy payloads, fixtures,
  tolerances, diagnostics, and failures.
- **Slice B — plain LOP oracle.** Land attraction/repulsion with shared kernels
  and deterministic analytic fixtures.
- **Slice C — WLOP density weighting.** Add the default density correction and
  its non-uniform/outlier contrast tests.
- **Slice D — smoke/docs.** Add executable correctness evidence before CLOP,
  EAR, or backend work starts.

## Right-sizing
- Use one module, typed strategy payloads, and plain result records. Do not add
  a backend interface/registry while only `cpu_reference` exists.
- Shared kernel extraction is justified by `METHOD-017`/`018`; all other
  implementation helpers stay file-local until a second caller exists.

## Backends
- Backend axis: `cpu_reference` only, with no request/fallback selector.
  `METHOD-019` owns the first real `cpu_optimized` identity and `METHOD-020`
  owns the runtime GPU backend.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/locally_optimal_projection/`.
- [ ] Fill `method.yaml` (`id: geometry.locally_optimal_projection`; metrics: `mean_distance_to_reference_surface`, `uniformity_min_pairwise_distance`, `iterations`, `runtime_ms`).
- [ ] Fill `paper.md` with both objectives/equations, density and repulsion
      conventions, coordinate/support-radius units, initialization/stopping
      rules, strategy defaults, numerical assumptions, source/revision/license
      provenance, diagnostics, and explicit failure states.

### Public API in `src/geometry`
- [ ] Add module `Geometry.PointCloud.Consolidation` (`.cppm` + `.cpp`) with
      typed `Lop`/`Wlop` strategy payloads, shared support radius `h`,
      repulsion weight `mu` in `[0, 0.5)`, iteration/target/initialization
      controls, and `Consolidate(...)` returning projected positions plus
      explicit status and convergence diagnostics. Record
      `cpu_reference` identity, but expose no request/fallback selector until
      `METHOD-019` adds the second implementation.
- [ ] Consume the shared `Geometry.PointCloud.Kernels` (`GEOM-062`) for the attraction/repulsion weights and WLOP density weights; no private weight math.
- [ ] Deterministic: seeded initialization and fixed iteration order; identical `(seed, input, params)` produce bitwise-identical output across runs and thread counts.
- [ ] Fail-closed on empty or too-small clouds, non-finite positions, `mu` outside [0, 0.5), and non-positive `h`, with explicit failure states.
- [ ] Register the module in `src/geometry/CMakeLists.txt` (single `IntrinsicGeometry` target; alphabetical placement, no new link dependency).

### Benchmarks
- [ ] Add executable manifest
      `benchmarks/geometry/manifests/locally_optimal_projection_reference_smoke.yaml`
      with stable ID `geometry.locally_optimal_projection.reference.smoke`,
      built-in noisy plane/sphere data, `intent: correctness`, fixed seed,
      explicit warmup/measured counts, and allowed metrics `runtime_ms` and
      `quality_error_l2`.
- [ ] Emit schema-valid `cpu_reference` result JSON with denoising,
      uniformity/outlier, iteration, strategy, and parameter diagnostics; no
      external dataset or performance claim.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` with `unit;geometry` labels.
- [ ] Denoising: on noisy plane and sphere fixtures, mean distance to the true surface strictly decreases versus the raw input and falls under a documented bound.
- [ ] Uniformity: repulsion (`mu > 0`) improves the `GEOM-036` min-pairwise-distance metric versus `mu = 0`.
- [ ] Outliers: sparse injected outliers do not pull the projected set beyond tolerance with WLOP density weights engaged.
- [ ] Plain-LOP contrast: a unit-weight run matches the frozen plain-LOP
      expectations on the plane fixture.
- [ ] Determinism and fail-closed cases as listed above.
- [ ] Freeze scale normalization and denoising/uniformity/outlier tolerances
      before assertions; cover zero neighborhoods, coincident points,
      non-convergence, and target-count/resource-cap failures.

## Docs
- [ ] `methods/geometry/locally_optimal_projection/README.md` with parameter-selection guidance (`h`, `mu`) and known limitations (thin structures, strongly anisotropic sampling).
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] WLOP is the default and plain LOP is available from the same
      implementation with density weighting disabled.
- [ ] All correctness tests pass in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Smoke result validates and reports quality/error plus convergence
      diagnostics for both LOP and WLOP.
- [ ] Public API type discipline: the exported surface uses only
      `std`/`glm`/scalar types plus the engine's own point-cloud types. The
      `Cloud`-taking `Consolidate(cloud, params)` entry is explicitly
      in-contract (vertex `PropertySet` access, matching the
      `Geometry.PointCloud.Normals` house pattern); no third-party types or
      method-internal solver state are exported.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|PointCloud|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without baseline.
- No normal-estimation or reconstruction coupling.
- No production dependency on framework24 and no copied implementation without
  recorded compatible-license provenance.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Wlop`/`Lop` reference strategies (correctness-first per the method workflow).
- `Operational` owned by `RUNTIME-175` for the config/runtime integration and
  by `UI-035` for the Sandbox panel; CLOP/EAR reference contracts remain
  `METHOD-017`/`018`. Optimized CPU and GPU parity are owned by
  `METHOD-019`/`020`.

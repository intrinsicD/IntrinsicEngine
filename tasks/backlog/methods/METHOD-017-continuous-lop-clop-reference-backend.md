---
id: METHOD-017
theme: I
depends_on: [METHOD-016, GEOM-058, GEOM-062]
maturity_target: CPUContracted
---
# METHOD-017 — Continuous LOP (CLOP) reference backend

## Goal
- Add the CLOP variant to the shared LOP-family consolidation surface: replace WLOP's discrete data term (a weighted sum over every input point) with a continuous term evaluated in closed form against a Gaussian-mixture model of the input, so projection cost scales with the mixture-component count instead of the raw point count.

## Non-goals
- No new consolidation module — CLOP is a `Strategy` variant on the `Geometry.PointCloud.Consolidation` surface introduced by `METHOD-016`, not a parallel module.
- No hierarchical/coarse-to-fine GMM acceleration or optimized data structures
  here; the reference builds one GMM at a chosen component count through
  `GEOM-058`. `METHOD-019` owns a concrete spatial-neighborhood optimized
  baseline; later `GEOM-060`/`061` adoption requires separate benchmark
  evidence and a separately scoped task.
- No GPU backend before reference parity (`METHOD-020`).
- No normal estimation, edge-aware behavior (that is `METHOD-018`), or surface reconstruction.

## Context
- Paper/method: Preiner, Mattausch, Arikan, Pajarola, Wimmer — "Continuous Projection for Fast L1 Reconstruction", ACM TOG 33(4) (SIGGRAPH 2014). The input point set is represented as a Gaussian mixture; the WLOP attraction integral over that continuous density has a closed form per mixture component, and the repulsion term is unchanged from WLOP.
- Method package: `methods/geometry/continuous_lop/` (manifest-only; id `geometry.continuous_lop`), following the `signed_heat` pattern — the executable reference lives in the shared `src/geometry` module, not in the package.
- Numerics gate: `GEOM-058` (`Geometry.GaussianMixture` — multivariate Gaussian, GMM, `FitEM` with k-means++ init) supplies the input density model; CLOP must not re-implement Gaussian/EM plumbing.
- Weighting gate: `GEOM-062` (`Geometry.PointCloud.Kernels`) supplies the repulsion function and radial weights CLOP shares with WLOP.
- Extends `METHOD-016`: CLOP is added to the same `ConsolidationParams::Strategy` axis so a caller chooses `Wlop` vs `Clop` through one API and one config/UI/agent surface. WLOP is the parity oracle — on a dense uniform fixture CLOP and WLOP must agree within a documented tolerance.

## Control surfaces
- Config/UI/Agent: none new in this task — CLOP is exposed as an additional `Strategy` enum value on the existing `Geometry.PointCloud.Consolidation` params. The runtime/config-lane and editor surfaces that make the strategy choosable are owned by `RUNTIME-175` / `UI-035`.

## Backends
- Backend axis: `cpu_reference` only. The concrete `cpu_optimized` spatial
  baseline is deferred to `METHOD-019`; `gpu_vulkan_compute` is deferred to
  `METHOD-020`.

## Slice plan
- **Slice A — intake/continuous-term contract.** Freeze GMM/objective units,
  normalization, component policy, stopping rules, fixtures, tolerances,
  diagnostics, and failure states.
- **Slice B — CLOP reference.** Add the closed-form continuous attraction term
  through the existing GMM and kernel seams, with deterministic analytic
  tests before any acceleration.
- **Slice C — WLOP contrast.** Prove the declared dense-uniform agreement and
  non-uniform/component-count behavior without turning work-count diagnostics
  into a speed claim.
- **Slice D — evidence/docs.** Add the executable correctness smoke and
  schema-valid result before METHOD-019/020 can consume the strategy.

## Right-sizing
- Add one typed `Clop` payload and one strategy branch to the shared module.
  Do not add a CLOP module, backend interface, mixture service, or hierarchy.
- Reuse `Geometry.GaussianMixture` and `Geometry.PointCloud.Kernels`; all
  closed-form assembly helpers remain file-local until a second caller needs
  the exact same operation.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/continuous_lop/`.
- [ ] Fill `method.yaml` (`id: geometry.continuous_lop`;
      `backends: [cpu_reference]`; metrics:
      `mean_distance_to_reference_surface`,
      `uniformity_min_pairwise_distance`, `mixture_component_count`,
      `iterations`, `runtime_ms`). `correctness_tests` and `benchmarks`
      resolve to real paths before this task can retire.
- [ ] Fill `paper.md` with the claim capture (closed-form continuous data term, component-count-bounded cost, expected quality versus WLOP on non-uniform input).
- [ ] Freeze coordinate/covariance units, mixture-weight normalization,
      closed-form objective, EM/projection stopping rules, scale-normalized
      fixtures, tolerances, and explicit failure states before implementation.
- [ ] Extend `Geometry.PointCloud.Consolidation` with a `Clop` strategy: a strategy payload struct (target `mixture_component_count` or component density, GMM regularization/`sigma²` floor passthrough to `GEOM-058`, plus the shared support radius `h`, repulsion weight `mu ∈ [0, 0.5)`, iteration count, seed) and a closed-form continuous attraction step evaluated over the fitted mixture; the repulsion term reuses `Geometry.PointCloud.Kernels`.
- [ ] Build the input GMM through `Geometry.GaussianMixture::FitEM` (seeded, k-means++ init); do not add a private mixture fitter.
- [ ] Deterministic: identical `(seed, input, params)` produce bitwise-identical output across runs and thread counts (seeded EM init + fixed iteration order).
- [ ] Fail-closed on empty/too-small clouds, non-finite positions, `mu` outside `[0, 0.5)`, non-positive `h`, and a requested component count exceeding the point count, with explicit failure states (no NaN/Inf).
- [ ] No `src/geometry/CMakeLists.txt` change if `Consolidation` already exists from `METHOD-016`; otherwise add nothing new beyond that module.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` (extend the `METHOD-016` file) with `unit;geometry` labels for the `Clop` strategy.
- [ ] Denoising: on noisy plane and sphere fixtures, CLOP mean distance to the true surface strictly decreases versus the raw input and falls under a documented bound.
- [ ] WLOP parity: on a dense, near-uniform fixture, CLOP with a sufficiently rich mixture matches the `METHOD-016` WLOP result within a documented tolerance.
- [ ] Component-count scaling: reducing `mixture_component_count` reduces the reported per-iteration work (component count is reported in diagnostics) while denoising stays within tolerance — cost tracks components, not raw point count.
- [ ] Outliers: sparse injected outliers do not pull the projected set beyond tolerance.
- [ ] Determinism and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/continuous_lop/README.md` with a backend-status table (`cpu_reference` → `METHOD-017`; optimized → `METHOD-019`; GPU → `METHOD-020`), WLOP-versus-CLOP guidance (when the continuous term wins), and known limitations (mixture-resolution/thin-structure trade-offs).
- [ ] Note the `Clop` strategy and its parameters in the `Geometry.PointCloud.Consolidation` interface documentation.
- [ ] Executable smoke manifest
      `benchmarks/geometry/manifests/continuous_lop_reference_smoke.yaml`
      (`benchmark_id: geometry.continuous_lop.reference.smoke`) on a stable
      built-in deterministic dataset, with `intent: correctness`, fixed seed,
      explicit warmup/measured counts, metrics `runtime_ms` and
      `quality_error_l2`, and schema-valid `cpu_reference` result JSON.
      Mixture resolution, WLOP parity, denoising/uniformity, EM/projection
      iterations, and failure status belong in diagnostics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `Clop` is selectable on the shared `ConsolidationParams::Strategy` axis alongside `Wlop`/`Lop`.
- [ ] All correctness tests (including WLOP parity) pass in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Emitted smoke result validates and reports quality/convergence
      diagnostics, not runtime alone.
- [ ] Public API exposes only `std`/`glm`/scalar types; the `GEOM-058` mixture backs the density model (no private Gaussian/EM code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|GaussianMixture|PointCloudKernels|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without a baseline (`METHOD-019` owns the speed claim versus WLOP).
- No private Gaussian-mixture or EM re-implementation.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Clop` reference strategy.
- `Operational` owned by `RUNTIME-175` for the config/runtime integration and
  by `UI-035` for the Sandbox panel; optimized CPU and GPU parity are owned by
  `METHOD-019`/`020`.

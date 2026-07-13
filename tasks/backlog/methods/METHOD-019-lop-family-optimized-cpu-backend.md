---
id: METHOD-019
theme: I
depends_on: [METHOD-016, METHOD-017, METHOD-018]
maturity_target: Operational
---
# METHOD-019 — LOP-family optimized CPU backend and comparison benchmark

## Goal
- Add an optimized CPU backend for the LOP-family consolidation surface (WLOP/LOP/CLOP/EAR) that matches the reference numerics within a documented parity tolerance while cutting runtime, and add the cross-variant comparison benchmark that gives the family's speed/quality claims a baseline.

## Non-goals
- No algorithm/variant changes and no quality-behavior changes versus the reference backends — this is an acceleration slice; the reference stays the canonical truth.
- No GPU backend (owned by `METHOD-020`).
- No new public parameters beyond a backend/precision selector and acceleration knobs; the strategy axis is unchanged.

## Context
- Owner/layer: `src/geometry` (the `Geometry.PointCloud.Consolidation` module and the `Geometry.PointCloud.Kernels` seam). `geometry -> core` only.
- Backend policy (`docs/architecture/algorithm-variant-dispatch.md`, `docs/methods/backend-policy.md`): the reference backend is canonical truth; the optimized backend must report backend identity and a measurable parity delta. This task introduces the `cpu_optimized` backend token; `Backend::CPU` resolves to `cpu_reference` or `cpu_optimized` per an explicit selector, and every `Result` reports which ran.
- Acceleration seams reused when available (not front-matter gates): `Geometry.KDTree`/grid neighborhoods for the WLOP/EAR local sums; the `GEOM-061` grid-downsampling reductions for coarse hierarchical GMM construction in CLOP; the `GEOM-060` permutohedral-lattice fast high-dimensional filtering seam as the CLOP continuous-term fast path and the EAR anisotropic-filter fast path. Ship grid-accelerated neighborhoods first; fold in `GEOM-060`/`GEOM-061` as those seams land.
- Benchmark policy (`docs/agent/benchmark-workflow.md`, `intrinsicengine-benchmark` skill): no speedup claim without a baseline comparison on a declared manifest.

## Control surfaces
- Config/UI/Agent: the backend selector is the existing `Backend`/CPU-precision field on the consolidation params (already the config/UI/agent override surface via `RUNTIME-175`/`UI-035`); no new surface here.

## Backends
- Backend axis: adds `cpu_optimized` to the family; `cpu_reference` stays the parity oracle. `gpu_vulkan_compute` deferred to `METHOD-020`.

## Required changes
- [ ] Add an optimized CPU path for each reference strategy (`Wlop`/`Lop`/`Clop`/`Ear`) behind an explicit selector, reusing `Geometry.KDTree`/grid neighborhoods and the shared `Geometry.PointCloud.Kernels`; no duplicated weight math.
- [ ] Report backend identity and parity on every `Result`: requested vs actual backend token (`cpu_reference`/`cpu_optimized`), and a parity-delta summary versus the reference where diagnostics carry it.
- [ ] Determinism preserved: the optimized path is bitwise-deterministic for identical `(seed, input, params)` across runs and thread counts (document any intentional summation-order change and bound its parity delta).
- [ ] Update `method.yaml` for the three packages (`locally_optimal_projection`, `continuous_lop`, `edge_aware_resampling`) to list `cpu_optimized` in `backends` and to add the comparison benchmark under `benchmarks`.

## Tests
- [ ] Extend `tests/unit/geometry/Test.PointCloudConsolidation.cpp` (`unit;geometry`) with parity tests: for each strategy the optimized output matches the reference within the documented tolerance on the standard fixtures.
- [ ] Backend telemetry: requesting `cpu_optimized` reports `cpu_optimized` as the actual backend; requesting `cpu_reference` reports `cpu_reference`.
- [ ] Determinism: optimized output is bitwise-stable across two runs and thread counts.
- [ ] Fail-closed parity: degenerate inputs return the same explicit failure states as the reference backend.

## Docs
- [ ] Comparison benchmark manifest `benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml` (`benchmark_id: geometry.lop_family.comparison.smoke`, `method: geometry.locally_optimal_projection`) reporting `runtime_ms`/`quality_error_l2` for reference vs optimized on the shared fixtures; a heavy/nightly variant is a named follow-up, not part of the smoke.
- [ ] Record the parity tolerance, the acceleration structures used, and the measured baseline in each package README backend-status table and `reports/`.
- [ ] Update the `Geometry.PointCloud.Consolidation` interface docs with the backend selector semantics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the surface changes.

## Acceptance criteria
- [ ] Optimized and reference backends produce parity-matching results for all four strategies within the documented tolerance, verified in the default CPU gate.
- [ ] Backend telemetry (`RequestedBackend`/`ActualBackend`) is asserted by tests.
- [ ] The comparison benchmark validates and runs, and the README records a reference-versus-optimized baseline (no bare speedup claim).
- [ ] Public API still exposes only `std`/`glm`/scalar types.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No quality/behavior divergence from the reference beyond the documented parity tolerance.
- No performance claim without the committed baseline comparison.
- No `std::rand` or global RNG state; no public Eigen types.

## Maturity
- Target: `Operational` (CPU) and parity-proven against the reference. This slice closes `CPUContracted → Operational` for the optimized CPU path; the `gpu_vulkan_compute` backend is owned by `METHOD-020`.

---
id: METHOD-018
theme: I
depends_on: [METHOD-016, GEOM-062]
maturity_target: CPUContracted
---
# METHOD-018 — Edge-Aware Resampling (EAR) and anisotropic feature-preserving LOP reference backend

## Goal
- Add the feature-preserving members of the LOP family — Edge-Aware Resampling and an anisotropic (normal-aware) WLOP weighting mode — to the shared consolidation surface so sharp edges and corners survive projection instead of being rounded off, giving the family a state-of-the-art edge-preserving option alongside isotropic WLOP/CLOP.

## Non-goals
- No new consolidation module — EAR and anisotropic weighting are `Strategy` variants / a weighting mode on the `Geometry.PointCloud.Consolidation` surface from `METHOD-016`.
- No normal *estimation* algorithm here — EAR consumes oriented normals from `Geometry.PointCloud.Normals` (or authored `v:normal`); this task adds the edge-aware bilateral normal *refinement* and the anisotropic projection, not a new estimator.
- No surface reconstruction or feature-line extraction as an output.
- No GPU/optimized backend before reference parity (owned by `METHOD-019`/`METHOD-020`).

## Context
- Paper/method: Huang, Wu, Gong, Cohen-Or, Ascher, Zhang — "Edge-Aware Point Set Resampling", ACM TOG 2013. EAR first resamples away from edges to obtain reliable normals (bilateral normal smoothing), then progressively upsamples toward edges under an anisotropic, normal-aware projection that preserves sharp features. The anisotropic WLOP weighting mode is the same normal-aware kernel applied to fixed-count consolidation.
- Method package: `methods/geometry/edge_aware_resampling/` (manifest-only; id `geometry.edge_aware_resampling`), `signed_heat` pattern — reference lives in the shared `src/geometry` module.
- Requires oriented normals: `Geometry.PointCloud.Normals` (PCA + MST orientation) supplies them when `v:normal` is absent; the bilateral normal refinement reuses the normal-aware weighting already present in `Geometry.PointCloud.Utils::BilateralFilter`.
- Weighting gate: `GEOM-062` (`Geometry.PointCloud.Kernels`) — EAR's anisotropic weight is a normal-aware extension of the shared radial kernel; add the anisotropic/directional weight to that kernel seam so it stays reusable rather than private to EAR. `Geometry::PCA::SymmetricEigen3` covers any local frame/anisotropy eigendecomposition without new linear algebra.
- Extends `METHOD-016`: `Ear` (and the anisotropic weighting flag) join the same `Strategy` axis so the feature-preserving option is chosen through one API/config/UI/agent surface. Isotropic WLOP is the contrast oracle — on a sharp-edge fixture EAR must retain the edge where isotropic WLOP demonstrably rounds it.

## Control surfaces
- Config/UI/Agent: none new in this task — exposed as an additional `Strategy` value plus an edge-sensitivity parameter on the existing consolidation params. Runtime/config-lane and editor exposure are owned by `RUNTIME-175` / `UI-035`.

## Backends
- Backend axis: `cpu_reference` only; optimized/GPU deferred to `METHOD-019`/`METHOD-020`.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/edge_aware_resampling/`.
- [ ] Fill `method.yaml` (`id: geometry.edge_aware_resampling`; `backends: [cpu_reference]`; metrics: `edge_sharpness_preservation`, `mean_distance_to_reference_surface`, `uniformity_min_pairwise_distance`, `iterations`, `runtime_ms`). `correctness_tests`/`benchmarks` resolve to real paths (or `TODO:`/`TBD` prefix).
- [ ] Fill `paper.md` (two-stage resample-away-then-upsample-toward-edges formulation, normal-aware anisotropic weight, oriented-normal precondition, degenerate/edge cases).
- [ ] Add a directional/anisotropic weight to `Geometry.PointCloud.Kernels` (`GEOM-062`): a normal-aware weight that attenuates contributions across a normal/feature discontinuity, exposed as a selectable weighting mode over `std`/`glm`/scalar types.
- [ ] Extend `Geometry.PointCloud.Consolidation` with an `Ear` strategy (edge-sensitivity, upsample target count, neighborhood size, `h`, iteration count, seed) and an anisotropic-weighting flag usable with the `Wlop` strategy; the projection consumes oriented normals and the bilateral-refined normals.
- [ ] Precondition on oriented normals: if `v:normal` is absent, invoke `Geometry.PointCloud.Normals` (documented, deterministic) or fail closed with an explicit "normals required" status — never project with undefined normals.
- [ ] Deterministic: seeded initialization and fixed iteration order; identical `(seed, input, params)` produce bitwise-identical output across runs and thread counts.
- [ ] Fail-closed on empty/too-small clouds, non-finite positions/normals, unoriented-normal degeneracy, and out-of-range parameters, with explicit failure states.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` (extend) with `unit;geometry` labels for `Ear` and the anisotropic weighting mode.
- [ ] Edge preservation: on a noisy two-plane dihedral / cube-edge fixture, EAR keeps the crease angle within a documented tolerance while isotropic WLOP on the same fixture demonstrably rounds it (quantified by the `edge_sharpness_preservation` metric).
- [ ] Denoising in flat regions: mean distance to the true surface strictly decreases away from edges and stays within a documented bound.
- [ ] Upsampling: the resampled count matches the requested target and new points concentrate near features without gaps beyond tolerance.
- [ ] Normal precondition: a cloud without normals either triggers deterministic estimation or returns the explicit "normals required" status; a cloud with authored `v:normal` uses it unchanged.
- [ ] Determinism and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/edge_aware_resampling/README.md` with a backend-status table, an isotropic-versus-edge-aware selection guide, the oriented-normal precondition, and known limitations (very high noise, ambiguous thin features).
- [ ] Document the `Ear` strategy, the anisotropic weighting mode, and the normal precondition in the `Geometry.PointCloud.Consolidation` and `Geometry.PointCloud.Kernels` interface docs.
- [ ] Smoke benchmark manifest `benchmarks/geometry/manifests/edge_aware_resampling_reference_smoke.yaml` (`benchmark_id: geometry.edge_aware_resampling.smoke`); benchmark metrics restricted to the enum (`runtime_ms`, `quality_error_l2`).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the kernel/consolidation surface changes.

## Acceptance criteria
- [ ] `Ear` and the anisotropic weighting mode are selectable on the shared `ConsolidationParams::Strategy`/weighting axis.
- [ ] The edge-preservation contrast test (EAR retains, isotropic WLOP rounds) passes in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Public API exposes only `std`/`glm`/scalar types.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|PointCloudKernels|PointCloudNormals' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without a baseline.
- No private normal estimator or private normal-aware weight (extend `Geometry.PointCloud.Normals` / `Geometry.PointCloud.Kernels`).
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Ear`/anisotropic reference strategies.
- `Operational` owned by `METHOD-019` (optimized CPU backend) and `METHOD-020` (GPU backend) after reference parity.

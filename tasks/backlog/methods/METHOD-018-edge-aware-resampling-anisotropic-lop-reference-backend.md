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

## Slice plan
- **Slice A — intake/precondition contract.** Freeze the EAR stages,
  anisotropic-weight equation, oriented-normal policy, units, fixtures,
  tolerances, diagnostics, and failures.
- **Slice B — directional kernel contrast.** Add and test the shared
  normal-aware weight against the isotropic kernel on analytic direction
  cases before integrating projection.
- **Slice C — EAR reference.** Add bilateral refinement and staged resampling,
  then prove edge retention, flat-region denoising, count, determinism, and
  fail-closed behavior.
- **Slice D — evidence/docs.** Add the executable correctness smoke and
  schema-valid result before optimized/GPU work starts.

## Right-sizing
- Extend the existing strategy/weighting payloads and the present shared kernel
  seam. Do not add an EAR module, normal service, strategy registry, or
  feature-line framework.
- Keep bilateral/stage-control helpers file-local; only the directional weight
  is shared because both EAR and anisotropic WLOP consume it now.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/edge_aware_resampling/`.
- [ ] Fill `method.yaml` (`id: geometry.edge_aware_resampling`;
      `backends: [cpu_reference]`; metrics:
      `edge_sharpness_preservation`, `mean_distance_to_reference_surface`,
      `uniformity_min_pairwise_distance`, `iterations`, `runtime_ms`).
      `correctness_tests` and `benchmarks` resolve to real paths before this
      task can retire.
- [ ] Fill `paper.md` (two-stage resample-away-then-upsample-toward-edges formulation, normal-aware anisotropic weight, oriented-normal precondition, degenerate/edge cases).
- [ ] Freeze position/normal/support-radius units, normal-orientation
      precondition, anisotropic-weight equation, stage/stop rules,
      scale-normalized fixtures, tolerances, and explicit failure diagnostics
      before implementation.
- [ ] Add a directional/anisotropic weight to `Geometry.PointCloud.Kernels` (`GEOM-062`): a normal-aware weight that attenuates contributions across a normal/feature discontinuity, exposed as a selectable weighting mode over `std`/`glm`/scalar types.
- [ ] Extend `Geometry.PointCloud.Consolidation` with an `Ear` strategy
      (edge-sensitivity, upsample target count, neighborhood size, `h`,
      iteration count, seed), an anisotropic-weighting flag usable with
      `Wlop`, and a plain normal-source policy:
      `AuthoredOrEstimate` (default) or `RequireAuthored`.
- [ ] Precondition on oriented normals: preserve and consume valid authored
      `v:normal`; otherwise the default policy invokes the deterministic
      `Geometry.PointCloud.Normals` path, while `RequireAuthored` fails closed
      with an explicit `NormalsRequired` status. Bilateral refinement operates
      on a method-local copy and never overwrites the authored property.
- [ ] Deterministic: seeded initialization and fixed iteration order; identical `(seed, input, params)` produce bitwise-identical output across runs and thread counts.
- [ ] Fail-closed on empty/too-small clouds, non-finite positions/normals, unoriented-normal degeneracy, and out-of-range parameters, with explicit failure states.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` (extend) with `unit;geometry` labels for `Ear` and the anisotropic weighting mode.
- [ ] Edge preservation: on a noisy two-plane dihedral / cube-edge fixture, EAR keeps the crease angle within a documented tolerance while isotropic WLOP on the same fixture demonstrably rounds it (quantified by the `edge_sharpness_preservation` metric).
- [ ] Denoising in flat regions: mean distance to the true surface strictly decreases away from edges and stays within a documented bound.
- [ ] Upsampling: the resampled count matches the requested target and new points concentrate near features without gaps beyond tolerance.
- [ ] Normal precondition: a cloud without normals deterministically estimates
      them under `AuthoredOrEstimate` and returns `NormalsRequired` under
      `RequireAuthored`; valid authored `v:normal` is consumed without
      mutation under both policies.
- [ ] Determinism and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/edge_aware_resampling/README.md` with a backend-status table, an isotropic-versus-edge-aware selection guide, the oriented-normal precondition, and known limitations (very high noise, ambiguous thin features).
- [ ] Document the `Ear` strategy, the anisotropic weighting mode, and the normal precondition in the `Geometry.PointCloud.Consolidation` and `Geometry.PointCloud.Kernels` interface docs.
- [ ] Executable smoke manifest
      `benchmarks/geometry/manifests/edge_aware_resampling_reference_smoke.yaml`
      (`benchmark_id: geometry.edge_aware_resampling.reference.smoke`) on a
      stable built-in dihedral/cube-edge dataset, with
      `intent: correctness`, fixed seed, explicit warmup/measured counts,
      metrics `runtime_ms` and `quality_error_l2`, and schema-valid
      `cpu_reference` result JSON. Edge sharpness, flat-region error,
      uniformity, output count, normal-source identity, and failures belong in
      diagnostics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the kernel/consolidation surface changes.

## Acceptance criteria
- [ ] `Ear` and the anisotropic weighting mode are selectable on the shared `ConsolidationParams::Strategy`/weighting axis.
- [ ] The edge-preservation contrast test (EAR retains, isotropic WLOP rounds) passes in the default CPU gate.
- [ ] Benchmark smoke manifest validates and runs.
- [ ] Emitted smoke result validates and records the isotropic contrast plus
      edge/flat-region quality, not runtime alone.
- [ ] Public API exposes only `std`/`glm`/scalar types.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|PointCloudKernels|PointCloudNormals|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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
- No optimized/GPU backend before reference parity; no performance claims without a baseline.
- No private normal estimator or private normal-aware weight (extend `Geometry.PointCloud.Normals` / `Geometry.PointCloud.Kernels`).
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Ear`/anisotropic reference strategies.
- `Operational` owned by `RUNTIME-175` for the config/runtime integration and
  by `UI-035` for the Sandbox panel; optimized CPU and GPU parity are owned by
  `METHOD-019`/`020`.

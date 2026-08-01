---
id: METHOD-018
theme: I
depends_on: [METHOD-016, GEOM-062]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner: "Codex-GeometryE2E"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-01T15:56:46Z"
maturity_target: CPUContracted
---
# METHOD-018 — Edge-Aware Resampling (EAR) and anisotropic feature-preserving LOP reference backend

## Status
- Completed on 2026-08-01 at `CPUContracted`. Literature intake covers the original 2013 EAR
  equations and code lineage plus later L0, graph-uniformity, intrinsic/
  isotropic, EC-Net, and cross-field upsampling improvements. The original
  deterministic two-phase EAR algorithm remains the correctness oracle; later
  objectives are comparison/deferred backends, not silent substitutions.
- Implementation commits: `fe88c995`, `488f2da0`; completion-evidence commit:
  pending.

## Goal
- Add a literature-faithful deterministic CPU reference for the original
  two-phase Edge-Aware Resampling algorithm and its anisotropic LOP operator to
  the shared consolidation surface, so sharp edges and corners have a bounded
  classical baseline alongside isotropic WLOP/CLOP.

## Non-goals
- No new consolidation module — EAR and anisotropic weighting are `Strategy` variants / a weighting mode on the `Geometry.PointCloud.Consolidation` surface from `METHOD-016`.
- No normal *estimation* algorithm here — EAR consumes oriented normals from `Geometry.PointCloud.Normals` (or authored `p:normal`); this task adds the edge-aware bilateral normal *refinement* and the anisotropic projection, not a new estimator.
- No surface reconstruction or feature-line extraction as an output.
- No GPU/optimized backend before reference parity (owned by `METHOD-019`/`METHOD-020`).

## Context
- Paper/method: Huang, Wu, Gong, Cohen-Or, Ascher, Zhang — "Edge-Aware Point Set Resampling", ACM TOG 32(1), 2013,
  DOI `10.1145/2421636.2421645`. EAR first resamples away from edges
  through alternating bilateral-normal refinement and anisotropic LOP, then
  progressively inserts oriented samples toward edges through midpoint
  clearance, edge-priority, bilateral projection-distance, and candidate-normal
  selection.
- Method package: `methods/geometry/edge_aware_resampling/` (manifest-only; id `geometry.edge_aware_resampling`), `signed_heat` pattern — reference lives in the shared `src/geometry` module.
- Requires oriented normals: `Geometry.PointCloud.Normals` (PCA + MST
  orientation) supplies them when the point cloud's built-in `p:normal`
  property is absent. Bilateral refinement operates on a private copy and uses
  the paper's signed normal-similarity equation; it never mutates authored
  normals.
- Weighting gate: `GEOM-062` (`Geometry.PointCloud.Kernels`) — EAR's anisotropic weight is a normal-aware extension of the shared radial kernel; add the anisotropic/directional weight to that kernel seam so it stays reusable rather than private to EAR. `Geometry::PCA::SymmetricEigen3` covers any local frame/anisotropy eigendecomposition without new linear algebra.
- Extends `METHOD-016`: `Ear` and anisotropic `Wlop` join the same `Strategy`
  axis so the feature-preserving option is chosen through one API/config/UI/
  agent surface. Isotropic WLOP is the contrast oracle on the same analytic
  dihedral fixture. This task makes no global state-of-the-art claim: later L0,
  graph/intrinsic, and learned resamplers address different robustness,
  uniformity, or data-prior trade-offs.

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
- [x] Clone `methods/_template/` to `methods/geometry/edge_aware_resampling/`.
- [x] Fill `method.yaml` (`id: geometry.edge_aware_resampling`;
      `backends: [cpu_reference]`; metrics:
      `edge_sharpness_preservation`, `mean_distance_to_reference_surface`,
      `uniformity_min_pairwise_distance`, `iterations`, `runtime_ms`).
      `correctness_tests` and `benchmarks` resolve to real paths before this
      task can retire.
- [x] Fill `paper.md` (two-stage resample-away-then-upsample-toward-edges formulation, normal-aware anisotropic weight, oriented-normal precondition, degenerate/edge cases).
- [x] Freeze position/normal/support-radius units, normal-orientation
      precondition, anisotropic-weight equation, stage/stop rules,
      scale-normalized fixtures, tolerances, and explicit failure diagnostics
      before implementation.
- [x] Add a directional/anisotropic weight to `Geometry.PointCloud.Kernels` (`GEOM-062`): a normal-aware weight that attenuates contributions across a normal/feature discontinuity, exposed as a selectable weighting mode over `std`/`glm`/scalar types.
- [x] Extend `Geometry.PointCloud.Consolidation` with an `Ear` strategy
      (edge-sensitivity, upsample target count, neighborhood size, `h`,
      iteration count, seed), an anisotropic-weighting flag usable with
      `Wlop`, and a plain normal-source policy:
      `AuthoredOrEstimate` (default) or `RequireAuthored`.
- [x] Precondition on oriented normals: preserve and consume valid authored
      `p:normal`; otherwise the default policy invokes the deterministic
      `Geometry.PointCloud.Normals` path, while `RequireAuthored` fails closed
      with an explicit `NormalsRequired` status. Bilateral refinement operates
      on a method-local copy and never overwrites the authored property.
- [x] Deterministic: seeded initialization and fixed iteration order; identical `(seed, input, params)` produce bitwise-identical output across runs and thread counts.
- [x] Fail-closed on empty/too-small clouds, non-finite positions/normals, unoriented-normal degeneracy, and out-of-range parameters, with explicit failure states.

## Tests
- [x] `tests/unit/geometry/Test.PointCloudConsolidation.cpp` (extend) with `unit;geometry` labels for `Ear` and the anisotropic weighting mode.
- [x] Edge preservation: on a noisy two-plane dihedral / cube-edge fixture, EAR keeps the crease angle within a documented tolerance while isotropic WLOP on the same fixture demonstrably rounds it (quantified by the `edge_sharpness_preservation` metric).
- [x] Denoising in flat regions: mean distance to the true surface strictly decreases away from edges and stays within a documented bound.
- [x] Upsampling: the resampled count matches the requested target and new points concentrate near features without gaps beyond tolerance.
- [x] Normal precondition: a cloud without normals deterministically estimates
      them under `AuthoredOrEstimate` and returns `NormalsRequired` under
      `RequireAuthored`; valid authored `p:normal` is consumed without
      mutation under both policies.
- [x] Determinism and fail-closed cases as listed above.

## Docs
- [x] `methods/geometry/edge_aware_resampling/README.md` with a backend-status table, an isotropic-versus-edge-aware selection guide, the oriented-normal precondition, and known limitations (very high noise, ambiguous thin features).
- [x] Document the `Ear` strategy, the anisotropic weighting mode, and the normal precondition in the `Geometry.PointCloud.Consolidation` and `Geometry.PointCloud.Kernels` interface docs.
- [x] Executable smoke manifest
      `benchmarks/geometry/manifests/edge_aware_resampling_reference_smoke.yaml`
      (`benchmark_id: geometry.edge_aware_resampling.reference.smoke`) on a
      stable built-in dihedral/cube-edge dataset, with
      `intent: correctness`, fixed seed, explicit warmup/measured counts,
      metrics `runtime_ms` and `quality_error_l2`, and schema-valid
      `cpu_reference` result JSON. Edge sharpness, flat-region error,
      uniformity, output count, normal-source identity, and failures belong in
      diagnostics.
- [x] Regenerate `docs/api/generated/module_inventory.md` if the kernel/consolidation surface changes.

## Acceptance criteria
- [x] `Ear` and the anisotropic weighting mode are selectable on the shared `ConsolidationParams::Strategy`/weighting axis.
- [x] The edge-preservation contrast test (EAR retains, isotropic WLOP rounds) passes in the default CPU gate.
- [x] Benchmark smoke manifest validates and runs.
- [x] Emitted smoke result validates and records the isotropic contrast plus
      edge/flat-region quality, not runtime alone.
- [x] Public API exposes only `std`/`glm`/scalar types.

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

## Verification evidence

- Clang 23 built `IntrinsicTests` and `IntrinsicBenchmarkSmoke`; the focused
  normals/consolidation/kernels selector passed all 42 cases.
- The default exclusion-only CPU gate passed 4,035/4,035 tests; the GLFW/LSan
  process test was intentionally skipped by its runtime capability check.
- Replacement-only `IntrinsicGeometryTests.Grouped` passed serially under both
  ASan and UBSan.
- The deterministic smoke preflight reports quality-error L2 `0.007731229`,
  reduces the input and isotropic-WLOP surface errors, preserves the declared
  edge contrast, returns all 80 requested samples with all eight inserted
  samples near the feature, and records authored-normal plus
  `normals_required` fail-closed diagnostics. The claim-grade schema-v2 seal
  and independent review remain the completion-evidence step.
- Strict method/benchmark manifests, layering, test layout, documentation
  links, generated module inventory, and `git diff --check` pass.

## Maturity
- Target: `CPUContracted` for the `Ear`/anisotropic reference strategies.
- `Operational` owned by `RUNTIME-175` for the config/runtime integration and
  by `UI-035` for the Sandbox panel; optimized CPU and GPU parity are owned by
  `METHOD-019`/`020`.

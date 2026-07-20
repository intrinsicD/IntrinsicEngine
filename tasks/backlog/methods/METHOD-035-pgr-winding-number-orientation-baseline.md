---
id: METHOD-035
theme: I
depends_on: []
maturity_target: CPUContracted
---
# METHOD-035 — Parametric Gauss (winding-number) orientation baseline (reference backend)

## Goal
- Add a CPU reference implementation of Parametric Gauss Reconstruction (PGR) normal orientation as the winding-number-family competitor baseline for the `METHOD-032` publication track: treat per-point surfel vectors as unknowns, require the Gauss winding-number formula evaluated at the sample points to equal 1/2 (the on-surface value), solve the regularized linear system with matrix-free fixed-iteration CG, and read oriented normals as the normalized solved surfels with magnitude as confidence.

## Non-goals
- No fast-multipole or hierarchical far-field acceleration — the reference is dense matrix-free `O(N^2)` per CG iteration, restricted to smoke-scale fixtures with an explicit input-size guard.
- No GCNO (winding-number-field regularization, Xu et al. 2023) — documented as the alternative family member; opens as its own task only if `METHOD-036` evidence shows the family needs a second representative.
- No optimized CPU or GPU backend; comparison protocol and report owned by `METHOD-036`.

## Context
- Paper/method: Lin, Wang, et al. — "Surface Reconstruction from Point Clouds without Normals by Parametrizing the Gauss Formula", TOG 2022 (PGR).
- Method package: `methods/geometry/parametric_gauss_orientation/`; implementation is package-local (`include/` + `src/`, the `progressive_poisson` pattern).
- Reuse: `Geometry.KDTree` for the near-field regularization width (kNN spacing); `Geometry.PointCloud.SurfaceSampling` for fixtures. The exported `Geometry.LinearSolver` targets assembled sparse systems; the dense matrix-free CG here is method-local by design (documented, not exported).
- Fully deterministic: no RNG anywhere; fixed CG iteration count and summation order.
- Metrics use the same names as `METHOD-032` (`oriented_correct_fraction`, `runtime_ms`) plus `confidence_mean` and `cg_residual` so results are directly comparable in `METHOD-036`.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/parametric_gauss_orientation/`; fill `method.yaml` (`id: geometry.parametric_gauss_orientation`; status `reference`; metrics above; paper block) and `paper.md`.
- [ ] Package-local reference implementation: params (regularization width factor, CG iteration count/tolerance, hard input-size cap), result (oriented normals, per-point confidence from surfel magnitude, status), diagnostics (residual history summary, capped-input rejection).
- [ ] Deterministic: identical `(input, params)` produce bitwise-identical outputs across runs and thread counts.
- [ ] Fail-closed with explicit statuses: empty/too-small input, non-finite data, input size above the documented `O(N^2)` guard, CG non-convergence beyond tolerance (distinct non-success status).

## Tests
- [ ] `tests/unit/geometry/Test.ParametricGaussOrientation.cpp` with `unit;geometry` labels.
- [ ] Scrambled-sign sphere and torus fixtures reach a documented `oriented_correct_fraction` bound.
- [ ] Determinism; input-size guard rejection; fail-closed cases above.

## Docs
- [ ] `methods/geometry/parametric_gauss_orientation/README.md` — parameter guidance (width factor, iteration count) and known limitations (`O(N^2)` scaling, sensitivity to regularization width on sparse regions, behavior near open boundaries).

## Acceptance criteria
- [ ] Reference implementation present and tested in the default CPU gate.
- [ ] `method.yaml` validates; benchmark smoke manifest validates and runs with quality metrics shared with `METHOD-032`.
- [ ] Public surface type discipline: `std`/`glm`/scalar plus engine point-cloud types; nothing method-internal exported.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ParametricGauss' --timeout 300
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No acceleration structures that change the reference numerics; no performance claims without baseline comparison.
- No `std::rand` or global RNG state; no external datasets in smoke tests.

## Maturity
- Target: `CPUContracted` (baseline-grade reference for the comparison protocol).
- No `Operational` follow-up is owed — this baseline exists for `METHOD-036` evidence, not for engine promotion.

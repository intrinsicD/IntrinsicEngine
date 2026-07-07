---
id: GEOM-058
theme: none
depends_on: []
maturity_target: CPUContracted
---
# GEOM-058 — Gaussian mixture models and Anderson-accelerated EM seam

## Goal
- Add a geometry-owned, deterministic Gaussian-mixture numerics seam — multivariate Gaussian evaluation, GMM representation, EM fitting with k-means++ initialization, and a reusable Anderson-acceleration fixed-point utility — as the numerics gate for the Coherent Point Drift method family (`METHOD-015`).

## Non-goals
- No point-set registration in this task (owned by `METHOD-015`).
- No GPU or optimized backend; CPU reference semantics only.
- No general-purpose optimization library; Anderson acceleration ships as a narrow fixed-point accelerator with damping/safeguarding, nothing more.
- No public Eigen types on the module surface; Eigen stays an implementation detail per `docs/architecture/geometry-api-style.md`.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- Port source: framework24 (`bcg_framework`, the same codebase family the retired port tasks reference as `bcg_code_base`): `lib_bcg_framework/include/bcg_gaussian_multivariate.h`, `bcg_gaussian_mixture.h`, `bcg_gaussian_mixture_fitting.h`, `bcg_anderson_acceleration.h`, `bcg_anderson_acceleration_new.h`, `bcg_dampled_anderson_acceleration_expectation_maximization.h`.
- The bcg originals carry two unconsolidated Anderson variants plus a typo'd third ("dampled") and ship without tests; this port consolidates them into one tested API instead of translating file-by-file.
- Reuses `Geometry.KMeans` for k-means++ seeding and follows the deterministic seeded-RNG conventions established by the `METHOD-004`/`METHOD-012` lineage.
- Gates `METHOD-015` (CPD family) — encoded in that task's `depends_on` front matter.

## Required changes
- [ ] Add module `Geometry.GaussianMixture` (`.cppm` interface + `.cpp` implementation unit): `MultivariateGaussian` (mean/covariance, log-pdf, seeded sampling), `GaussianMixture` (weights + components, responsibility evaluation, log-likelihood), and `FitEM(points, k, params)` with k-means++ initialization, a covariance regularization floor, and an explicit iteration/convergence report.
- [ ] Add a windowed Anderson-acceleration utility for fixed-point iterations (own small module or a partition of an existing numerics module, whichever review prefers) with damping and residual-safeguarded fallback to plain iteration.
- [ ] `FitEM` accepts an acceleration policy (`None` | `Anderson`) so plain EM and accelerated EM share one code path.
- [ ] Deterministic seeding: identical `(seed, input)` produce bitwise-identical fits across runs and thread counts.
- [ ] Fail-closed handling for empty input, `k == 0`, `k` greater than the point count, and duplicate/coincident points (the regularization floor engages instead of producing non-finite values).
- [ ] Register modules in `src/geometry/CMakeLists.txt`; no umbrella re-export.

## Tests
- [ ] `tests/unit/geometry/Test.GaussianMixture.cpp` with `unit;geometry` labels.
- [ ] Recovery: synthetic 2- and 3-component mixtures with known parameters are recovered within documented tolerance.
- [ ] Monotonicity: plain-EM log-likelihood is non-decreasing across iterations on fixtures.
- [ ] Acceleration: Anderson-accelerated EM reaches the same optimum as plain EM within tolerance in fewer or equal iterations on a fixture (iteration-count assertion, no wall-clock claim).
- [ ] Robustness: the degenerate inputs above return explicit failure states or floored covariances; no NaN/Inf escapes.
- [ ] Determinism: same seed produces bitwise-identical parameters across two runs.

## Docs
- [ ] Interface documentation per geometry API style, including the regularization-floor and failure-state contract.
- [ ] Regenerate the module inventory.
- [ ] Update the port-gap cluster notes in `tasks/backlog/geometry/README.md`.

## Acceptance criteria
- [ ] Public surface exposes only `std`/`glm`/scalar types.
- [ ] All listed tests pass in the default CPU gate.
- [ ] `METHOD-015` can express its E-step against this surface without private includes.
- [ ] Layering check passes (`geometry -> core` only).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GaussianMixture|Anderson' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No registration/CPD logic in this task.
- No `std::rand`/`std::default_random_engine`; RNG derives from explicit seeds only.
- No public Eigen types on module interfaces.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed — optimized/GPU variants open per the method workflow only when a consumer needs them.

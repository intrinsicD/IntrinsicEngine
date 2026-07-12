---
id: GEOM-059
theme: I
depends_on: []
maturity_target: CPUContracted
---
# GEOM-059 — Kernel matrices, Nyström approximation, and Gaussian-process interpolation seam

## Goal
- Add a geometry-owned kernel-methods seam: reusable kernel functions, dense kernel-matrix assembly, Nyström low-rank approximation, and Gaussian-process interpolation (posterior mean plus variance) for scattered-data interpolation over point clouds and mesh vertices.

## Non-goals
- No hyperparameter learning or marginal-likelihood optimization in this slice; kernel parameters are explicit caller inputs.
- No sparse/inducing-point GP machinery beyond the Nyström seam.
- No GPU backend.
- No public Eigen types on module interfaces.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- Port source: framework24 `lib_bcg_framework/include/bcg_function_kernels.h`, `bcg_matrix_kernels.h`, `bcg_matrix_kernel_types.h`, `bcg_matrix_kernel_nystroem_method.h`, `bcg_gaussian_process_interpolation.h`.
- The bcg originals are header-only and untested; this port adds explicit SPD safeguards (jitter escalation with a capped, reported failure state) and correctness tests.
- Dense factorizations stay internal (Eigen LLT/LDLT); the sparse solver seams from retired `GEOM-020`/`GEOM-023` are untouched.
- Engine motivation: scattered scalar-field interpolation on scanned point clouds and mesh vertices (for example, propagating sparse measurements or editing weights) without requiring a mesh Laplacian solve.

## Required changes
- [ ] Add module `Geometry.Kernels` (`.cppm` + `.cpp`): kernel evaluators (Gaussian/RBF and Laplacian at minimum) with explicit bandwidth parameters, plus dense kernel-matrix assembly for `K(X, Y)`.
- [ ] Add `NystroemApproximation(points, landmark_indices, kernel)` returning the low-rank factor with a documented reconstruction-error contract.
- [ ] Add Gaussian-process interpolation (same module or a sibling partition): `Interpolate(train_points, train_values, query_points, kernel, noise, params)` returning posterior mean and variance; jitter escalation for near-singular kernel matrices with an explicit failure state when the escalation cap is reached.
- [ ] Deterministic seeded landmark-selection helper for Nyström; callers may also pass explicit landmark indices.
- [ ] Fail-closed on empty inputs, mismatched train point/value counts, non-positive bandwidth, and non-finite inputs.
- [ ] Register modules in `src/geometry/CMakeLists.txt`.

## Tests
- [ ] `tests/unit/geometry/Test.Kernels.cpp` and GP interpolation coverage with `unit;geometry` labels.
- [ ] Interpolation: a noise-free GP reproduces training values at training points within tolerance; posterior variance is approximately zero there and non-negative everywhere.
- [ ] Analytic fixture: GP mean on a smooth function beats a nearest-neighbor baseline error on held-out points.
- [ ] Nyström: reconstruction error decreases monotonically with landmark count on a fixture.
- [ ] Degenerates: duplicate training points (jitter engages), empty inputs, and zero bandwidth return explicit failure states; no NaN/Inf escapes.
- [ ] Determinism: seeded landmark selection is bitwise stable across runs.

## Docs
- [ ] Interface documentation per geometry API style; document the jitter/failure contract and complexity bounds (dense `O(n^3)` solve, Nyström `O(n m^2)`).
- [ ] Regenerate the module inventory.
- [ ] Update the port-gap cluster notes in `tasks/backlog/geometry/README.md`.

## Acceptance criteria
- [ ] Public surface exposes only `std`/`glm`/scalar types (spans in and out).
- [ ] All listed tests pass in the default CPU gate.
- [ ] Layering check passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Kernels|GaussianProcess' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No hyperparameter optimization or learning loops.
- No new third-party dependency; dense factorizations use the existing internal Eigen path.
- No public Eigen types on module interfaces.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.

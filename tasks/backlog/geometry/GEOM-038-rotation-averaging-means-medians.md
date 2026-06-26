---
id: GEOM-038
theme: none
depends_on: [GEOM-037]
maturity_target: CPUContracted
---
# GEOM-038 — Rotation averaging — SO(3) means and medians

## Goal
- Add deterministic SO(3) rotation-averaging routines — L2 means (chordal, geodesic/Karcher, quaternion) and robust L1 medians (geodesic Weiszfeld, quaternion Weiszfeld) — as a new `src/geometry` module `Geometry.RotationAveraging`, layered on the GEOM-037 rotation primitives and reusing `ComputeSymmetricEigen` for the quaternion-moment eigenproblem.
- Every routine accepts an optional per-sample weight vector and returns the averaged rotation alongside an iteration/convergence diagnostics struct, failing closed (no asserts, no NaNs) on empty, single-sample, antipodal/degenerate, or non-finite input.

## Non-goals
- No UI or editor tooling. A rotation-averaging visualization/editor surface, if ever wanted, is a separate `src/runtime` task; geometry never depends on runtime.
- No registration changes. Robust weighting inside registration is `GEOM-048` and is out of scope here.
- No GPU/compute backend; this is a CPU-only contract.
- No new acceleration structures or linear-algebra primitives; reuse `Geometry.Linalg` / `Geometry.PCA` `ComputeSymmetricEigen` and the GEOM-037 rotation/quaternion types.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only). New module `Geometry.RotationAveraging` depends on `Geometry.Rotation` (GEOM-037) and `Geometry.PCA` / `Geometry.Linalg` (`ComputeSymmetricEigen`). It must not import assets/runtime/graphics/rhi/ecs/app.
- IntrinsicEngine currently has zero rotation-averaging machinery. `ComputeSymmetricEigen` exists in `src/geometry/Geometry.Linalg.cppm` (exported through `Geometry.PCA`) but is never wired to a 4x4 quaternion moment matrix, and there is no chordal/Karcher/Weiszfeld code anywhere in `src/geometry`.
- Ported feature surface (free functions, geometry-native quaternion/rotation types from GEOM-037):
  - Chordal L2 mean (`bcg_rotation_mean_chordal`): top eigenvector of the 4x4 quaternion moment matrix (Markley method), or equivalently SO(3) projection of the weighted average rotation matrix; supports weights and optional outlier rejection.
  - Geodesic / Karcher (Fréchet L2) mean (`bcg_rotation_mean_geodesic`): iterative Riemannian averaging in the tangent space via log/exp to a convergence tolerance / max-iteration bound.
  - Quaternion mean (`bcg_rotation_mean_quaternion`): fast sign-consistent (hemisphere-aligned) linear quaternion averaging.
  - Geodesic L1 median (`bcg_rotation_median_geodesic`): Weiszfeld inverse-distance reweighting on SO(3).
  - Quaternion L1 median (`bcg_rotation_median_quaternion`): angle-axis/quaternion Weiszfeld L1 median.
- Follow `GEOM-005` (API/numeric policy) and `GEOM-007` (robust-predicate/tolerance policy): explicit, fail-closed diagnostics on degenerate/empty/non-finite input; deterministic for a fixed input + parameters.

## Slice plan
- [ ] Slice A (means): implement chordal L2, Karcher geodesic, and sign-consistent quaternion means with weights + diagnostics, plus their unit tests. Defers all medians to Slice B; median entry points may be declared but must fail closed as not-yet-implemented if exported in this slice.
- [ ] Slice B (medians): implement geodesic Weiszfeld L1 median and quaternion Weiszfeld L1 median with weights + diagnostics, plus their robustness/outlier tests.

## Required changes
- [ ] Add module interface `src/geometry/Geometry.RotationAveraging.cppm` (`export module Geometry.RotationAveraging;`) declaring the averaging free functions, a shared options struct (per-sample weights, convergence tolerance, max iterations, optional outlier-rejection threshold), a result struct (averaged rotation + iteration count + achieved residual + converged/diagnostic flag), and an explicit error/status enum. Export only declarations and small inline/templated helpers.
- [ ] Add implementation unit `src/geometry/Geometry.RotationAveraging.cpp` containing the non-trivial bodies: chordal mean via the 4x4 quaternion moment matrix solved by `ComputeSymmetricEigen`; SO(3) projection fallback; Karcher iteration via rotation log/exp from `Geometry.Rotation`; sign-consistent quaternion mean; and (Slice B) the geodesic and quaternion Weiszfeld L1 medians.
- [ ] Register the module in `src/geometry/CMakeLists.txt` via `intrinsic_add_module_library` / `target_sources(... FILE_SET CXX_MODULES ...)`, wiring dependencies on `Geometry.Rotation`, `Geometry.PCA`, and `Geometry.Linalg` (no new external deps).
- [ ] Implement fail-closed handling for: empty sample set, single sample (returns it unchanged with converged=true, zero iterations), antipodal pair / no unique mean, weight vector size mismatch, non-positive or non-finite weights, non-finite rotation inputs, and non-convergence within max iterations — each surfaced through the status enum and diagnostics, never via assert or NaN return.
- [ ] Ensure all routines are deterministic (fixed iteration order, no unordered traversal, no floating-point reductions whose order varies by sample count) and self-consistent across the chordal/Karcher/quaternion code paths on shared helpers.

## Tests
- [ ] Add `unit;geometry` test `tests/geometry/RotationAveraging.*` covering identity: averaging N copies of the same rotation (all five routines) returns that rotation within tolerance.
- [ ] Add a test that chordal L2 and Karcher geodesic means agree (within a documented tolerance) for tightly clustered inputs.
- [ ] Add a robustness test: with injected gross-outlier rotations, the geodesic and quaternion L1 medians stay near the inlier cluster while the L2 means are pulled away (median-vs-mean separation exceeds a threshold).
- [ ] Add a weighted-mean test: a weighted 2-sample mean matches the analytic slerp midpoint/weighted-slerp interpolation between the two rotations.
- [ ] Add a determinism test: identical inputs + options produce bit-stable results across repeated calls and across input permutations where the routine is order-independent.
- [ ] Add fail-closed tests: empty set, single sample, antipodal pair, weight/sample size mismatch, and non-finite input each return the documented error status with no NaN and no crash.
- [ ] Keep tests under the existing `unit;geometry` label; introduce no new CTest labels unless `tests/README.md` and `tests/CMakeLists.txt` are updated in the same change.

## Docs
- [ ] Document the five routines (definitions, the Markley quaternion-moment-matrix derivation, Karcher iteration, Weiszfeld reweighting), their options/diagnostics, and the fail-closed contract in [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md).
- [ ] Regenerate `docs/api/generated/module_inventory.md` to include `Geometry.RotationAveraging`.
- [ ] Cross-reference `GEOM-037` (primitives) and note `GEOM-048` as the out-of-scope registration follow-up.

## Acceptance criteria
- [ ] `Geometry.RotationAveraging` exposes deterministic chordal L2, Karcher geodesic, and sign-consistent quaternion means, plus geodesic and quaternion Weiszfeld L1 medians, each accepting optional per-sample weights and returning rotation + iteration/convergence diagnostics.
- [ ] The chordal mean is computed from the 4x4 quaternion moment matrix via `ComputeSymmetricEigen` (the eigen routine is now wired to a quaternion moment matrix).
- [ ] Averaging N copies of one rotation returns it; chordal and Karcher agree on clustered inputs; the L1 medians are demonstrably more outlier-robust than the L2 means; a weighted 2-sample mean equals the analytic slerp midpoint — all within documented tolerances.
- [ ] Empty, single-sample, antipodal, mismatched-weight, and non-finite inputs fail closed with explicit status and no NaN.
- [ ] `geometry -> core` layering is preserved (no assets/runtime/graphics/rhi/ecs/app imports); `check_layering.py --strict` passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RotationAveraging|Rotation|PCA' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work (UI/editor tooling, registration weighting, new sampling/method logic).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies from the geometry layer.
- Adding a GPU/compute backend or new linear-algebra/acceleration primitives instead of reusing `ComputeSymmetricEigen` and the GEOM-037 rotation types.
- Claiming performance improvements without a baseline comparison.

## Maturity
- Target: `CPUContracted` (deterministic CPU rotation-averaging API with analytic/robustness tests and a fail-closed contract).
- No `Operational` follow-up is owed; this task has no GPU/backend seam and no runtime/UI integration in scope.

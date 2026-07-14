---
id: GEOM-037
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---

# GEOM-037 — SO(3) rotation primitives (Lie machinery)

## Goal

- [x] Add a reusable geometry-layer SO(3) Lie-algebra primitive module `Geometry.Rotation` so the engine has first-class rotation math (hat/vee, exp/log, geodesic & chordal distances, uniform random rotations, optimal-rotation/Procrustes, and ProjectOnSO).
- [x] Extract and generalize the Kabsch/Umeyama optimal-rotation logic currently buried as private statics in `Geometry.Registration` into `Geometry.Rotation`, and have `Geometry.Registration` call the new module instead of its inline copy.
- [x] Establish the shared foundation that `GEOM-038` (rotation averaging) and `Geometry.Registration` both build on, with deterministic, fail-closed behavior per GEOM-005 and GEOM-007.

## Non-goals

- No rotation averaging (means/medians, Karcher/geometric means); that is owned by GEOM-038.
- No quaternion or quaternion-spline interpolation (slerp/squad).
- No runtime, ECS, graphics, RHI, assets, app, or platform dependencies; this is pure geometry-layer math.
- No GPU backend; this is deterministic CPU math only.
- No rotation representation type proliferation (no new quaternion type, no Euler-angle conventions) beyond what hat/vee/exp/log require.

## Context

- `bcg` (`bcg_rotation_utils`) keeps these as free functions: hat/vee, Rodrigues exp, matrix log, angular and chordal distances, uniform random rotations, and optimal rotation.
- IntrinsicEngine currently has only an inline Rodrigues construction plus a Kabsch/Umeyama optimal-rotation step buried as private helpers in the anonymous namespace of `src/geometry/Geometry.Registration.cpp` (see the `namespace { ... }` block, the alignment/transform-fitting helpers used by the registration pipeline).
- `Geometry.Linalg` already provides `PolarDecompositionResult` with an `Orthogonal` member (`src/geometry/Geometry.Linalg.cppm`, `struct PolarDecompositionResult { DenseMatrix Orthogonal; DenseMatrix Symmetric; NumericDiagnostics Diagnostics; }` and `ComputePolarDecomposition`), which is an effective polar projection onto the orthogonal group; `ProjectOnSO` must reuse this rather than reimplementing an SVD/polar decomposition.
- `Geometry.PCA` (`src/geometry/Geometry.PCA.cppm`) provides eigen/PCA primitives that registration already depends on; the new module should depend only on `core` and existing geometry-layer math modules (`Geometry.Linalg`), never upward.
- Layering: `Geometry.Rotation` lives in the geometry layer and must NOT import assets/runtime/graphics/rhi/ecs/app. `core -> nothing`; `geometry -> core` only.
- Status: completed 2026-06-28 by Codex. Commit: this commit (`Complete SO3 rotation primitive extraction`).
- `Geometry.Rotation` now ships the CPU SO(3) primitive surface as
  `Hat`/`Vee`, `Exp`/`Log`, `AngularDistance`, `ChordalDistance`,
  `RandomRotation`, `ProjectOnSO3`, and `OptimalRotation` overloads for
  `glm::vec3` and `glm::dvec3` correspondences. The shipped public contract uses
  deterministic fail-closed sentinels (`identity` for rotation-valued routines,
  zero vector for invalid `Log`) rather than separate public result records, and
  the fail-closed behavior is covered by unit tests.
- `Geometry.Registration` now imports `Geometry.Rotation` for point-to-point ICP
  alignment; the private Kabsch/Umeyama symmetric-eigensolver copy was removed.

## Slice plan

- [x] Slice 1 — Module scaffold: create `Geometry.Rotation.cppm` + `Geometry.Rotation.cpp`, register via `intrinsic_add_module_library` / `FILE_SET CXX_MODULES`, export the public surface (hat, vee, exp, log, distances, ProjectOnSO, optimal rotation, uniform random rotation) with fail-closed stubs and diagnostics; wire into the geometry CMake target and module inventory.
- [x] Slice 2 — Implement Lie machinery: hat/vee, `RotationMatrixExponential` (Rodrigues with small-angle series fallback), `RotationMatrixLogarithm` (SafeAcos-clamped, handling angle ~0 and ~pi), angular (geodesic) distance, chordal (Frobenius) distance, and seeded deterministic uniform random rotation.
- [x] Slice 3 — Optimal rotation + projection: implement `ProjectOnSO` reusing `Geometry.Linalg::ComputePolarDecomposition().Orthogonal` (with a determinant-sign correction to stay in SO(3) rather than O(3)), and the generalized Kabsch/Umeyama Procrustes solver.
- [x] Slice 4 — Registration migration: replace the private Kabsch/Umeyama statics in `Geometry.Registration.cpp` with calls into `Geometry.Rotation`, preserving existing registration behavior and tests.

## Required changes

- [x] Create `src/geometry/Geometry.Rotation.cppm` exporting `namespace Geometry::Rotation` with: `Hat(vector)->3x3` (skew/antisymmetric) and `Vee(3x3)->vector`; `RotationMatrixExponential(axisAngle)`; `RotationMatrixLogarithm(matrix)`; `AngularDistance(R0, R1)` (geodesic); `ChordalDistance(R0, R1)` (Frobenius); `ProjectOnSO(matrix)`; `OptimalRotation(...)` / `Procrustes(...)` (Kabsch/Umeyama over corresponded point sets); `UniformRandomRotation(seed)`. Export only types/decls and small inline/templates; keep non-trivial bodies in the implementation unit.
- [x] Create `src/geometry/Geometry.Rotation.cpp` (module implementation unit `module Geometry.Rotation;`) holding the non-trivial bodies: Rodrigues exp with small-angle series fallback; log with `SafeAcos` domain clamping and explicit angle ~0 and ~pi branches; distance computations; `ProjectOnSO` delegating to `Geometry.Linalg::ComputePolarDecomposition(...).Orthogonal` with determinant-sign correction; Kabsch/Umeyama solver; seeded deterministic RNG for `UniformRandomRotation`.
- [x] Define the public fail-closed contract for every entry point so
      degenerate/empty/non-finite input returns a finite sentinel: no asserts,
      no NaNs returned, no UB. The final shipped API uses sentinel return values
      rather than separate public result/diagnostic records for these compact
      math primitives.
- [x] Update `src/geometry/CMakeLists.txt` to add `Geometry.Rotation` via `intrinsic_add_module_library` and `target_sources(... FILE_SET CXX_MODULES FILES Geometry.Rotation.cppm)`, linking against `Geometry.Linalg` (and `core`); ensure `Geometry.Registration` links `Geometry.Rotation`.
- [x] Refactor `src/geometry/Geometry.Registration.cpp`: remove the private inline Kabsch/Umeyama optimal-rotation statics from its anonymous namespace and replace their call sites with `Geometry::Rotation::OptimalRotation` / `Procrustes`, preserving the existing registration result shape and numeric behavior (no behavior change visible to callers/tests).
- [x] Regenerate `docs/api/generated/module_inventory.md` so it lists `Geometry.Rotation`.

## Tests

- [x] Add `tests/geometry/` coverage labeled `unit;geometry` (no new CTest labels) for `Geometry.Rotation`.
- [x] Round-trip: `RotationMatrixLogarithm(RotationMatrixExponential(w))` recovers `w` within tolerance for representative `w` (including near-zero and near-pi magnitudes), and `RotationMatrixExponential(RotationMatrixLogarithm(R))` recovers `R` within tolerance for random `R in SO(3)`.
- [x] Hat/vee inverse: `Vee(Hat(w)) == w` exactly/within tolerance, and `Hat(w)` is antisymmetric (`M + Mᵀ == 0`).
- [x] Distances: `AngularDistance(R, R) == 0` and `ChordalDistance(R, R) == 0` (identity); both are symmetric (`d(A,B) == d(B,A)`); angular distance equals the rotation angle of `Aᵀ B`.
- [x] `ProjectOnSO` is idempotent on inputs already in SO(3) (`ProjectOnSO(R) == R` within tolerance) and orthonormalizes a perturbed matrix (output is orthogonal with `det == +1`).
- [x] Procrustes/optimal rotation recovers a known rotation from a corresponded point set transformed by that rotation (within tolerance), including the noise-free exact case.
- [x] Determinism: `UniformRandomRotation(seed)` returns bit-identical results across repeated calls with the same seed and across runs; distinct seeds produce distinct rotations; every output is in SO(3).
- [x] Fail-closed: degenerate/edge inputs (log at angle 0 and angle pi,
      non-finite/NaN matrix entries, empty or under-determined point sets for
      Procrustes) return finite sentinel results, never a NaN result or assert.

## Docs

- [x] Document `Geometry.Rotation`'s public surface and numeric/fail-closed policy in the geometry layer docs / module notes consistent with GEOM-005 and GEOM-007.
- [x] Note in the relevant doc that `ProjectOnSO` reuses `Geometry.Linalg`'s polar decomposition and that optimal-rotation logic was extracted from `Geometry.Registration`.
- [x] Ensure `docs/api/generated/module_inventory.md` reflects the new module (regenerated, not hand-edited).

## Acceptance criteria

- [x] `Geometry.Rotation.cppm` + `Geometry.Rotation.cpp` exist in `src/geometry/`, build under the `ci` preset, and are listed in `docs/api/generated/module_inventory.md`.
- [x] All new `unit;geometry` tests pass; the round-trip, hat/vee, distance, ProjectOnSO, Procrustes, determinism, and fail-closed cases above are each exercised.
- [x] `Geometry.Registration` no longer contains its own inline Kabsch/Umeyama optimal-rotation statics and instead calls `Geometry.Rotation`; existing registration tests pass unchanged.
- [x] `ProjectOnSO` delegates to `Geometry.Linalg::ComputePolarDecomposition` (verified by inspection) rather than reimplementing a polar/SVD decomposition.
- [x] `python3 tools/repo/check_layering.py --root src --strict` passes: `Geometry.Rotation` imports only `core` and geometry-layer math; no runtime/graphics/rhi/ecs/assets/app imports.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` and the docs-link/test-layout checks pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Geometry\.(Rotation|Registration)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing the mechanical extraction (moving Kabsch/Umeyama out of `Geometry.Registration`) with any semantic change to registration behavior, numeric policy, or result shapes in the same change.
- Introducing unrelated feature work (rotation averaging, quaternion interpolation, new representation types) — those belong to GEOM-038 or are out of scope.
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into the geometry layer.
- Reimplementing a polar/SVD decomposition inside `Geometry.Rotation` instead of reusing `Geometry.Linalg`'s `ComputePolarDecomposition`.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Claiming performance improvements without a baseline comparison.

## Maturity

- Stop-state pin: this task closes at `CPUContracted`. The module ships deterministic, fail-closed, contract-tested CPU rotation primitives with the public surface above fully implemented (not stubbed) and exercised by passing `unit;geometry` tests.
- No `Operational` follow-up is owed: this is pure deterministic CPU math with no runtime integration, no GPU backend, and no parity backend to reconcile.

---
id: GEOM-023
theme: none
depends_on: []
---
# GEOM-023 — Sparse non-symmetric iterative solver seam (BiCGSTAB)

## Goal
- Add a non-symmetric sparse iterative solver seam (BiCGSTAB with optional
  incomplete-LU preconditioning) to `Geometry.Sparse` so method packages whose
  operators are not SPD have a first-class CPU reference solver, complementing
  the symmetric CG path from retired GEOM-008 and the direct SPD factorization
  seam owned by `GEOM-020`.

## Non-goals
- No GMRES in this task. Eigen ships GMRES only in its `unsupported/` tree;
  if BiCGSTAB proves insufficient for a concrete consumer, file a separate
  follow-up that owns the `unsupported`-module quality/license review.
- No direct sparse LU (`SparseLU`) path; file a follow-up if a consumer needs
  factor-once / solve-many on non-symmetric systems.
- No changes to the existing CG / shifted-CG or LDLT/LLT seams.
- No changes to public method-package APIs; this task only extends the
  geometry solver seam consumed by future method backends.
- No performance claims without benchmark baselines.

## Context
- Status: backlog (not yet promoted).
- Owner/agent: unassigned.
- Owning subsystem/layer: `geometry` (`geometry -> core`; reuses the
  already-declared Eigen3 dependency from GEOM-008 — `Eigen::BiCGSTAB` and
  `Eigen::IncompleteLUT` live in core Eigen `IterativeLinearSolvers`, so no
  new third-party addition and no `EIGEN_MPL2_ONLY` conflict).
- Filed as the non-symmetric follow-up that GEOM-020's Non-goals anticipated
  ("method packages that need it can file a separate follow-up").
- Concrete consumer: [`METHOD-003`](../methods/METHOD-003-closest-point-method-pde-reference-backend.md)
  variant A (Closest Point Method with interior boundary conditions) assembles
  a non-symmetric `L_band` operator in its Step 5 and is gated on this seam.
  Promote this task when METHOD-003 is the next-priority method.
- Mirror the API shape of GEOM-020's `SparseLDLT`/`SparseLLT` wrappers so
  consumers can switch solver families without relearning diagnostics
  conventions.

## Required changes
- [ ] Extend `src/geometry/Geometry.Sparse.cppm` with a `SparseBiCGSTAB`
      wrapper over the existing `SparseMatrix` CSR type. Provide
      `solve(matrix, rhs, params)` plus a multi-RHS overload, with `params`
      covering max iterations, relative tolerance, and preconditioner choice
      (`None`, `Diagonal`, `IncompleteLUT`).
- [ ] Reuse the `SparseFactorizationStatus`-style reporting idiom from
      GEOM-020: a status enum (`Success`, `NotConverged`, `NumericalIssue`,
      `DimensionMismatch`, `InvalidInput`) and a diagnostics struct (status,
      iterations used, final relative residual, preconditioner used).
- [ ] Implement the wrapper in `src/geometry/Geometry.Sparse.cpp` with
      explicit input validation (square matrix, finite entries, finite RHS)
      and translate Eigen's `ComputationInfo` to the status enum.
- [ ] Pin determinism: fixed input plus fixed params must produce bitwise
      identical iterates (single-threaded Eigen path; document the
      `EIGEN_DONT_PARALLELIZE`/thread assumptions the contract relies on).
- [ ] Keep `Geometry.Sparse` re-exported through `Geometry.cppm` as it
      already is; do not widen `Geometry.Linalg` re-export rules.
- [ ] Update `src/geometry/CMakeLists.txt` only if a new translation unit is
      added; no new module-interface file should be required.

## Tests
- [ ] Add `tests/unit/geometry/Test.SparseNonsymmetric.cpp` (labels:
      `unit;geometry`) with deterministic cases: a small advection-diffusion
      operator (genuinely non-symmetric) with known solution; an SPD matrix
      cross-checked against the CG path within tolerance; a singular matrix
      asserting `NotConverged`/`NumericalIssue` reporting; non-square /
      non-finite input rejection; multi-RHS round-trip; and a
      preconditioner-on/off agreement check within tolerance.
- [ ] Add a regression test asserting bit-stable output for identical
      `(matrix, rhs, params)` across two runs.
- [ ] Default CPU gate must remain green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Extend the `docs/architecture/geometry.md` "Linear algebra policy"
      section with the non-symmetric solver row: CG/shifted-CG for SPD
      iterative, LDLT/LLT for SPD factor-once / solve-many, BiCGSTAB for
      non-symmetric iterative.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module
      surface changes (additions live inside the existing `Geometry.Sparse`
      module, so it should not).

## Acceptance criteria
- [ ] `Geometry.Sparse` exposes a `SparseBiCGSTAB` solver with structured
      diagnostics and the documented preconditioner options.
- [ ] All new unit tests pass under the default CPU gate; no
      `flaky-quarantine` label is introduced.
- [ ] No new third-party dependency is added; the existing Eigen3 cache
      continues to satisfy the build offline.
- [ ] Layering / test-layout / docs-link / task-policy validators remain
      green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SparseNonsymmetric|Sparse|DEC' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Do not add GMRES or any Eigen `unsupported/` module in this task.
- Do not add SuiteSparse / MKL / other external solver backends.
- Do not widen the `Geometry.Linalg` re-export rules; the narrow /
  explicit-import boundary established by GEOM-008 stays as-is.
- Do not mix mechanical file moves with semantic refactors.
- Do not claim performance superiority over existing solver paths without a
  benchmark baseline.

## Maturity
- Target: `CPUContracted`. The seam is a CPU-only reference solver; there is
  no GPU equivalent owed by this task. No `Operational` follow-up is owed.

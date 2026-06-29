---
id: GEOM-020
theme: none
depends_on: []
completed_on: 2026-06-28
---
# GEOM-020 — Sparse direct factorization solver seam (LDLT/LLT)

## Goal
- Add a direct sparse symmetric factorization seam (`SimplicialLDLT` / `SimplicialLLT`) to `Geometry.Sparse` so method packages that need a deterministic, factor-once / solve-many path for SPD systems have a first-class CPU reference solver, complementing the iterative CG solver already shipped by GEOM-008.

## Non-goals
- No optional SuiteSparse / CHOLMOD / Pardiso / Accelerate / MKL backends in this task; document the seam shape so a later task can add them.
- No non-symmetric sparse LU; the non-symmetric **iterative** seam (BiCGSTAB) is owned by retired follow-up [`GEOM-023`](GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md), and a direct non-symmetric LU stays unowned until a consumer needs it.
- No sparse symmetric (generalized) eigensolver; that is METHOD-006's gap and is owned by follow-up [`GEOM-024`](../backlog/geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md) (see Context).
- No changes to public method-package APIs; this task only adds to the geometry solver seam consumed by future method backends.
- No performance claims without benchmark baselines.

## Context
- Status: retired at `CPUContracted`.
- Owner/agent: Codex.
- Owning subsystem/layer: `geometry` (`geometry -> core`; reuses the already-declared Eigen3 dependency from GEOM-008).
- Promoted from the GEOM-008 follow-up gap noted in
  [`tasks/done/GEOM-008-linear-algebra-solver-infrastructure.md`](GEOM-008-linear-algebra-solver-infrastructure.md).
  GEOM-008 closed at `CPUContracted` shipping `Geometry.Linalg`
  (dense decompositions) and `Geometry.Sparse` (CSR builders + CG +
  shifted CG). It did **not** ship a direct sparse factorization, but
  `METHOD-002` (retired) and `tasks/backlog/methods/METHOD-003` (step 5)
  both name the "LDLT path from `GEOM-008`" as the solver they intend
  to call. This task owns that gap so those method packages have a
  concrete seam name before promotion.
- Backend choice: `Eigen::SimplicialLDLT` and `Eigen::SimplicialLLT`
  are header-only inside the existing Eigen3 dependency and require no
  new third-party additions. AMD ordering is built-in.
- Separate gap (not in scope here): METHOD-006 step 4 expects a sparse
  symmetric generalized eigensolver (LOBPCG / shift-invert) from
  GEOM-008. That requires Spectra (an Eigen-companion library) and a
  different API surface; that follow-up is filed as
  [`GEOM-024`](../backlog/geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md),
  which depends on this task for its shift-invert inner solve.

## Status
- Completed at `CPUContracted`. Commit: this commit (`Complete sparse direct factorization seam`).
- `Geometry.Sparse` now exposes `SparseLDLT` and `SparseLLT` over the existing CSR type, with `factor`, span-based single-RHS solves, `Eigen::Ref` multi-RHS solves, and solve-in-place overloads.
- Factorization reports `SparseFactorizationDiagnostics` with status, pivot count, smallest absolute pivot, and a reserved condition-estimate field. LDLT classifies negative pivots as `NonSPD` and near-zero pivots as `ZeroPivot`; LLT uses Eigen status plus an LDLT probe for failure classification.
- The default DEC/geodesic CG path remains unchanged; the direct seam is available for future method packages that need factor-once / solve-many SPD solves.

## Required changes
- [x] Extend `src/geometry/Geometry.Sparse.cppm` with `SparseLDLT` and
      `SparseLLT` wrappers around `Eigen::SimplicialLDLT` /
      `Eigen::SimplicialLLT` over the existing `SparseMatrix` CSR type.
      Provide `factor(matrix)` / `solve(rhs)` / `solveInPlace(x)` plus
      a multi-RHS overload taking an `Eigen::Ref` of a dense block.
- [x] Add a `SparseFactorizationStatus` enum (`Success`,
      `NotFactored`, `NumericalIssue`, `NonSPD`, `ZeroPivot`,
      `DimensionMismatch`, `InvalidInput`) and a
      `SparseFactorizationDiagnostics` struct (status, pivot count,
      smallest absolute pivot, condition-estimate placeholder for a
      later slice) returned by the factor step.
- [x] Implement the wrappers in `src/geometry/Geometry.Sparse.cpp`
      with explicit input validation (square matrix, finite entries,
      symmetric in debug builds) and translate Eigen's
      `ComputationInfo` to the new status enum.
- [x] Keep `Geometry.Sparse` re-exported through `Geometry.cppm` as it
      already is; do not widen `Geometry.Linalg` re-export rules.
- [x] Update `src/geometry/CMakeLists.txt` only if a new translation
      unit is added; no new module-interface file should be required.

## Tests
- [x] Add `tests/unit/geometry/Test.SparseFactorization.cpp` (labels:
      `unit;geometry`) with deterministic small-system cases: a 1-D
      Poisson stiffness matrix (SPD); the cotan-Laplacian-plus-mass
      operator `M + tL` on a small triangle fan (the matrix shape
      METHOD-002 will solve); a synthetically indefinite matrix to
      assert `NonSPD` / `NumericalIssue` reporting; a singular matrix
      to assert `ZeroPivot`; multi-RHS round-trip; and a non-square /
      non-finite input rejection battery.
- [x] Add a regression test that re-solves the same factorization
      against several RHS vectors and asserts bit-stable output (the
      factor-once / solve-many contract method packages depend on).
- [x] Default CPU gate must remain green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [x] Extend `docs/architecture/geometry.md` "Linear algebra policy"
      section to document the direct vs iterative solver split:
      `Geometry.Sparse::SolveCG` / `SolveCGShifted` for matrix-free /
      large iterative solves; `Geometry.Sparse::SparseLDLT` /
      `SparseLLT` for SPD factor-once / solve-many. Keep the existing
      Spectra / SuiteSparse "later optional backend" paragraph as-is.
- [x] Regenerate `docs/api/generated/module_inventory.md` if the
      module surface changes (it should not — the additions live
      inside the existing `Geometry.Sparse` module).
- [x] Update retired [`tasks/done/METHODS-001`](METHODS-001-signed-heat-pathfinder.md)
      and [`tasks/backlog/methods/README.md`](../backlog/methods/README.md)
      ordering notes only if the gate language drifts; the
      pre-promotion edits made together with the GEOM-008 retirement
      already point METHOD-002 at this task for the LDLT path.

## Acceptance criteria
- [x] `Geometry.Sparse` exposes `SparseLDLT` and `SparseLLT` types
      with `factor` / `solve` / `solveInPlace` APIs and a structured
      diagnostics return on `factor`.
- [x] All new unit tests pass under the default CPU gate; no
      `flaky-quarantine` label is introduced.
- [x] No new third-party dependency is added; the existing Eigen3
      cache continues to satisfy the build offline.
- [x] Layering / test-layout / docs-link / task-policy validators
      remain green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'SparseFactorization|Sparse|DEC' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Do not add SuiteSparse / CHOLMOD / Pardiso / Accelerate / MKL backends in this task.
- Do not add a sparse symmetric (generalized) eigensolver in this task; that is METHOD-006's gap and belongs in a separate follow-up.
- Do not widen the `Geometry.Linalg` re-export rules; the narrow / explicit-import boundary established by GEOM-008 stays as-is.
- Do not mix mechanical file moves with semantic refactors.
- Do not claim performance superiority over the existing CG path without a benchmark baseline.

## Maturity
- Target: `CPUContracted`. The seam is a CPU-only reference solver;
  there is no GPU equivalent owed by this task. Optional backend
  promotion (SuiteSparse / CHOLMOD) is explicitly deferred to a
  separate later task and would not change the seam's maturity from
  the geometry layer's perspective. No `Operational` follow-up is owed.

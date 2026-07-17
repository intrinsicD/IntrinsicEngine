---
id: GEOM-070
theme: I
depends_on: []
maturity_target: CPUContracted
---
# GEOM-070 — Rectangular sparse LSQR and LSCM adoption

## Goal
- Add a geometry-owned rectangular sparse least-squares free function that avoids normal equations, and migrate the existing LSCM implementation to it in the same slice.

## Non-goals
- No underdetermined minimum-norm guarantee, dense public matrix API, SparseQR class, KKT system, arbitrary equality constraints, or general optimization package.
- No fixed-variable implicit-smoothing work; correctness there is owned independently by `BUG-110`.
- The new LSQR API adds no public Eigen types or third-party dependency; cleanup of unrelated existing Eigen-ref overloads in `Geometry.Sparse` is out of scope.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- `Geometry.Sparse::SparseMatrix` is already rectangular CSR and exposes `Multiply`/`MultiplyTranspose`, but its solver surface covers square CG/factorization/BiCGSTAB only.
- `Geometry.HalfedgeMesh.Parameterization.cpp::ComputeLSCM` currently forms `A^T A` and `A^T b` privately and applies CG, squaring the condition number of the conformal least-squares problem.
- `bcg_geometry_processing/include/SparseMatrixLeastSquaresSolver.h` demonstrates the old demand but is explicitly an anti-port oracle: it forms normal equations, mixes soft constraints/KKT-style hard constraints and console output, and carries no focused test contract. An LSQR-style free function preserves LSCM's existing tolerance and iteration controls and fits IntrinsicEngine's current sparse value types.

## Required changes
- [ ] Add plain LSQR params/result/diagnostics and a free solve function to the existing `Geometry.Sparse` module, using CSR `Multiply` and `MultiplyTranspose` without forming `A^T A`.
- [ ] Scope the supported shape to the overdetermined `m >= n` case required by LSCM and define dimension, empty, non-finite, breakdown/degeneracy, convergence, and max-iteration results explicitly.
- [ ] Validate public CSR structure before the first multiply: row-offset size/monotonic/final-count agreement, value/column length agreement, and column bounds must fail closed rather than reaching unchecked indexing.
- [ ] Preserve caller-controlled relative tolerance and maximum iterations; any damping/regularization knob must be finite, nonnegative, and justified by the LSCM caller.
- [ ] Build LSCM's rectangular system with the canonical sparse builder and solve it directly through LSQR.
- [ ] Remove LSCM-local `ComputeAtA`/`ComputeAtb` code and make public/result comments solver-neutral (`SolverIterations`, convergence/status) without introducing a second editor/config knob.
- [ ] Keep any Eigen use added for LSQR implementation/test oracles behind implementation/test boundaries; add no new Eigen name to the exported LSQR signature.

## Tests
- [ ] Compare exact and noisy tall systems against a dense QR/least-squares oracle within documented residual/solution tolerances.
- [ ] Cover shape mismatch, invalid tolerance/iteration/damping, non-finite matrix/RHS, malformed CSR variants, degenerate columns, breakdown, and explicit non-convergence.
- [ ] Assert deterministic diagnostics and results across repeated calls.
- [ ] Keep existing LSCM disk fixtures, pins, finite UVs, flip/distortion diagnostics, and strategy-dispatch behavior passing.
- [ ] Add structural coverage proving the parameterization implementation no longer contains or calls private normal-equation builders.

## Docs
- [ ] Document the supported shape, convergence/residual meanings, failure states, and lack of a minimum-norm promise in `Geometry.Sparse`.
- [ ] Update LSCM comments to describe direct rectangular least squares rather than CG on normal equations.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if needed and update the geometry backlog notes.

## Acceptance criteria
- [ ] LSCM reaches its accepted solution/diagnostics without materializing `A^T A` or `A^T b`.
- [ ] Existing LSCM tolerance and iteration control surfaces remain truthful.
- [ ] The new LSQR surface contains plain data and free functions only and exposes no Eigen type.
- [ ] Geometry tests and layering checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Sparse|Parameterization|LSCM' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Forming normal equations under a differently named helper.
- Adding a solver class/factory or promising unsupported underdetermined semantics.
- Changing LSCM controls solely to match a new implementation convenience.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.

---
id: GEOM-024
theme: I
depends_on: [GEOM-020]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: Sparse matrix algebra and a private dependency; no method package, ECS property binding/publication, or shared parameterization optimization change. Public API review still applies.
---
# GEOM-024 — Sparse symmetric generalized eigensolver seam

## Completion — 2026-09-27
Commit: the enclosing `claude/geom-024-sparse-eigensolver` commit records this
retirement. CPUContracted: `Geometry.Sparse::SolveSymmetricGeneralizedEigen` with
analytic, dense-oracle and failure-mode tests. Spectra was rejected (exceptions);
option D, a hand-rolled shift-invert subspace iteration, was selected by the
operator. The eigenbasis viewer follow-up is UI-056.

## Goal
- Add a sparse symmetric generalized eigensolver seam to `Geometry.Sparse`
  that computes the smallest eigenpairs of `A z = λ M z` for SPD mass `M` and
  symmetric `A`, so spectral methods (cross fields, spectral processing) have
  a first-class CPU reference path.

## Non-goals
- No dense eigensolvers beyond a small-system test oracle; `Geometry.Linalg`
  already owns dense decompositions.
- No non-symmetric eigenproblems.
- No full spectrum computation; the seam targets the k smallest (or
  shift-targeted) eigenpairs only.
- No changes to public method-package APIs.
- No performance claims without benchmark baselines.

## Context
- Status: backlog (not yet promoted).
- Owner/agent: unassigned.
- Owning subsystem/layer: `geometry` (`geometry -> core`).
- Filed as the eigensolver follow-up that GEOM-020's Context anticipated
  ("a separate follow-up task should be filed when METHOD-006 is the
  next-priority method"). Promote this task when METHOD-006 is the
  next-priority method.
- Concrete consumer: [`METHOD-006`](../backlog/methods/METHOD-006-cross-field-design-reference-backend.md)
  variant B (Knöppel et al. globally optimal direction fields) solves the
  smallest-eigenvalue problem `A z = λ M z` in its Step 4 and is gated on
  this seam. `docs/roadmap.md` also lists spectral mesh processing as a
  remaining geometry-processing capability that will reuse it.
- GEOM-020 is delivered: `SparseLDLT` in `Geometry.Sparse` supplies the
  factorization of `(A - σM)` needed by shift-invert. This satisfied dependency
  is the existing inner solve; the generalized eigensolver remains missing.

## Backend options and default selection

The selected backend is marked below; the others remain possible follow-ups
behind the same public seam.

- Not selected: **A — Spectra (header-only Eigen companion, MPL-2.0 license — not MIT as first recorded),
  `SymGEigsShiftSolver` shift-invert mode over `Geometry.Sparse` LDLT.**
  Mature ARPACK-style Lanczos implementation, no binary dependency, pairs
  directly with the existing Eigen3 usage. **Rejected 2026-09-27:** Spectra throws in
  65 places with no opt-out and cannot compile under the project-wide
  `-fno-exceptions` / "never use exceptions" policy; the operator chose a
  hand-rolled solver over an exception island or a patched port.
  Requires adding Spectra through `vcpkg.json` with the repository's pinned
  baseline/override/overlay policy; the vcpkg cutover is already authoritative.
- Not selected: **B — Hand-rolled LOBPCG over `Geometry.Sparse` CSR.** No new
  dependency, but block-iteration robustness (orthogonalization, breakdown
  handling) is non-trivial to get right; only pick if adding Spectra is
  rejected.
- Test oracle only: **C — Dense fallback via `Geometry.Linalg` for small systems.** Not a
  production seam; ships only as the test oracle inside the unit tests.
- Selected: **D — Shift-invert block subspace iteration (Bathe) over `SparseLDLT`.**
  **Selected 2026-09-27.** No dependency, deterministic, Rayleigh-Ritz in the
  M-inner product with a backward-error convergence test; converges at
  `(lambda_i - sigma) / (lambda_(q+1) - sigma)` per iteration, about 20–26
  iterations on the tested grids. LOBPCG (B) remains an option if block iteration
  proves too slow for large k.

Record the dependency decision in the `docs/architecture/geometry.md`
"Linear algebra policy" section when implementing; an ADR is not owed unless
the reviewer disputes the Spectra pick (the policy section already
anticipates a Spectra-class addition).

## Required changes
- [x] (Superseded) Add and pin Spectra through `vcpkg.json` — rejected with option A; no dependency added.
- [x] Extend `src/geometry/Geometry.Sparse.cppm` with the eigensolver: implemented as the free
      function `SolveSymmetricGeneralizedEigen(A, M, params)` (sparse or diagonal `M`; a stateless
      class added nothing), returning the k smallest eigenpairs; `params` cover count, shift, max
      iterations, tolerance, block (subspace) dimension and seed.
- [x] Reuse the GEOM-020 reporting idiom: a status enum (`Success`,
      `NotConverged`, `NumericalIssue`, `DimensionMismatch`, `InvalidInput`)
      and a diagnostics struct (status, converged eigenpair count, iterations,
      residual norms).
- [x] Implement in `src/geometry/Geometry.Sparse.Eigensolver.cpp` (shift-invert subspace iteration
      with the `GEOM-020` LDLT factorization as the inner solve); validate inputs (square, matching
      dimensions, finite entries, symmetry, and SPD `M` in every build).
- [x] Pin determinism: fixed input, fixed seed/start vector, and fixed params
      must produce reproducible eigenpairs (document the sign/ordering
      convention for eigenvectors).
- [x] Keep solver internals out of public geometry APIs; results use `std` types only.

## Tests
- [x] Add `tests/unit/geometry/Test.SparseEigensolver.cpp` (labels:
      `unit;geometry`) with deterministic cases: 1-D Laplacian with analytic
      eigenvalues; cotan-Laplacian + lumped mass on a small fan cross-checked
      against a dense `Geometry.Linalg` oracle; k larger than matrix size
      rejected as `InvalidInput`; non-SPD `M` flagged (in every build); and a
      reproducibility check across two runs. Also: Neumann nullspace with the default shift,
      indefinite `A` with default versus explicit shift, and `NotConverged` reporting.
- [x] Default CPU gate must remain green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [x] Extend the `docs/architecture/geometry.md` "Linear algebra policy"
      section with the eigensolver row and the Spectra dependency decision.
- [x] Update `docs/build-troubleshooting.md` only if the new dependency
      changes the offline-cache workflow (no dependency added; no change).
- [x] Regenerate `docs/api/generated/module_inventory.md` if the module
      surface changes (no new module; inventory unchanged).

## Priority note (2026-09-27 Framework24 gap audit)

This is the only nontrivial numerical capability still missing relative to
Framework24 (`bcg_sparse_matrix_eigendecomposition.h`, Spectra). It unblocks manifold
harmonics and spectral filtering, HKS/WKS descriptors, SCP (METHOD-024) and cross
fields (METHOD-006); raise its priority. Framework24 also has an eigendecomposition
viewer (eigenvalue bar chart, eigenvector shown as a scalar field); allocate its
editor follow-up once this seam lands.

## Acceptance criteria
- [x] `Geometry.Sparse` exposes a generalized symmetric eigensolver returning
      the k smallest eigenpairs with structured diagnostics.
- [x] Analytic and dense-oracle tests pass under the default CPU gate; no
      `flaky-quarantine` label is introduced.
- [x] Spectra (or the explicitly recorded alternative) is wired through the
      vcpkg manifest with a pinned version; no Spectra types leak
      through public APIs. — The recorded alternative (option D) needs no dependency.
- [x] Layering / test-layout / docs-link / task-policy validators remain
      green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SparseEigensolver|Sparse|DEC' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- No FetchContent, direct downloads, or dependency route outside vcpkg.
- Do not expose Spectra or Eigen types through public geometry APIs.
- Do not compute or promise full-spectrum decompositions.
- Do not add ARPACK, SLEPc, or binary eigensolver dependencies.
- Do not widen the `Geometry.Linalg` re-export rules.
- Do not mix mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted`. The seam is a CPU-only reference solver; there is
  no GPU equivalent owed by this task. No `Operational` follow-up is owed.

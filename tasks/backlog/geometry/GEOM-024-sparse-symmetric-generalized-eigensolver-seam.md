---
id: GEOM-024
theme: I
depends_on: [GEOM-020]
---
# GEOM-024 — Sparse symmetric generalized eigensolver seam

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
- Concrete consumer: [`METHOD-006`](../methods/METHOD-006-cross-field-design-reference-backend.md)
  variant B (Knöppel et al. globally optimal direction fields) solves the
  smallest-eigenvalue problem `A z = λ M z` in its Step 4 and is gated on
  this seam. `docs/roadmap.md` also lists spectral mesh processing as a
  remaining geometry-processing capability that will reuse it.
- Depends on `GEOM-020` because the recommended shift-invert backend needs a
  sparse factorization of `(A - σM)` as its inner solve; the LDLT seam is
  that factorization.

## Backend options and default selection

Mark `[x]` next to the backend that the seam wraps first. Unmarked options
become follow-ups behind the same public seam.

- [x] **A — Spectra (header-only Eigen companion, MIT license),
  `SymGEigsShiftSolver` shift-invert mode over `Geometry.Sparse` LDLT.**
  Mature ARPACK-style Lanczos implementation, no binary dependency, pairs
  directly with the existing Eigen3 usage. **Selected as the default.**
  Requires adding Spectra through `cmake/Dependencies.cmake` +
  `external/cache/` (coordinate with `INFRA-001` if the vcpkg cutover lands
  first; Spectra is also a first-class vcpkg port).
- [ ] **B — Hand-rolled LOBPCG over `Geometry.Sparse` CSR.** No new
  dependency, but block-iteration robustness (orthogonalization, breakdown
  handling) is non-trivial to get right; only pick if adding Spectra is
  rejected.
- [ ] **C — Dense fallback via `Geometry.Linalg` for small systems.** Not a
  production seam; ships only as the test oracle inside the unit tests.

Record the dependency decision in the `docs/architecture/geometry.md`
"Linear algebra policy" section when implementing; an ADR is not owed unless
the reviewer disputes the Spectra pick (the policy section already
anticipates a Spectra-class addition).

## Required changes
- [ ] Add Spectra to `cmake/Dependencies.cmake` (FetchContent, header-only,
      cached under `external/cache/`) or via the vcpkg manifest if `INFRA-001`
      has landed; pin a release tag.
- [ ] Extend `src/geometry/Geometry.Sparse.cppm` with a
      `SparseSymmetricEigensolver` wrapper: `solve(A, M, k, params)` returning
      the k smallest eigenpairs, with `params` covering shift, max iterations,
      tolerance, and Lanczos subspace dimension.
- [ ] Reuse the GEOM-020 reporting idiom: a status enum (`Success`,
      `NotConverged`, `NumericalIssue`, `DimensionMismatch`, `InvalidInput`)
      and a diagnostics struct (status, converged eigenpair count, iterations,
      residual norms).
- [ ] Implement in `src/geometry/Geometry.Sparse.cpp` using Spectra's
      shift-invert mode with the `GEOM-020` LDLT factorization as the inner
      solve; validate inputs (square, matching dimensions, finite entries,
      SPD `M` in debug builds).
- [ ] Pin determinism: fixed input, fixed seed/start vector, and fixed params
      must produce reproducible eigenpairs (document the sign/ordering
      convention for eigenvectors).
- [ ] Keep Spectra types out of all public geometry APIs; the wrapper is the
      only surface consumers see.

## Tests
- [ ] Add `tests/unit/geometry/Test.SparseEigensolver.cpp` (labels:
      `unit;geometry`) with deterministic cases: 1-D Laplacian with analytic
      eigenvalues; cotan-Laplacian + lumped mass on a small fan cross-checked
      against a dense `Geometry.Linalg` oracle; k larger than matrix size
      rejected as `InvalidInput`; non-SPD `M` flagged in debug; and a
      reproducibility check across two runs.
- [ ] Default CPU gate must remain green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.

## Docs
- [ ] Extend the `docs/architecture/geometry.md` "Linear algebra policy"
      section with the eigensolver row and the Spectra dependency decision.
- [ ] Update `docs/build-troubleshooting.md` only if the new dependency
      changes the offline-cache workflow.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module
      surface changes.

## Acceptance criteria
- [ ] `Geometry.Sparse` exposes a generalized symmetric eigensolver returning
      the k smallest eigenpairs with structured diagnostics.
- [ ] Analytic and dense-oracle tests pass under the default CPU gate; no
      `flaky-quarantine` label is introduced.
- [ ] Spectra (or the explicitly recorded alternative) is wired through the
      central dependency mechanism with a pinned tag; no Spectra types leak
      through public APIs.
- [ ] Layering / test-layout / docs-link / task-policy validators remain
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
- Do not expose Spectra or Eigen types through public geometry APIs.
- Do not compute or promise full-spectrum decompositions.
- Do not add ARPACK, SLEPc, or binary eigensolver dependencies.
- Do not widen the `Geometry.Linalg` re-export rules.
- Do not mix mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted`. The seam is a CPU-only reference solver; there is
  no GPU equivalent owed by this task. No `Operational` follow-up is owed.

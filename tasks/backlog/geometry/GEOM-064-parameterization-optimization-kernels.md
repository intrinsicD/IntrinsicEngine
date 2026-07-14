---
id: GEOM-064
theme: I
depends_on: [GEOM-018]
maturity_target: CPUContracted
---
# GEOM-064 — Parameterization optimization kernels seam

## Goal
- Add a reusable geometry-owned kernel seam for iterative distortion-minimizing parameterization — per-triangle local rotation/Jacobian fitting, the symmetric-Dirichlet (and ARAP) energy/gradient plus a PSD-projected proxy system, and an injectivity-preserving maximum-step line search — so the ARAP/SLIM/progressive family shares one tested nonlinear-solve core instead of each variant re-deriving the same local-step, flip-barrier, and line-search math privately.

## Non-goals
- No solver control flow here — the local/global outer loop and per-method schedule stay in each method (`METHOD-021` ARAP, `METHOD-022` SLIM); this task ships only the stateless per-triangle and line-search primitives they consume.
- No dispatch surface (that is `GEOM-063`) and no boundary-mapping policy (that is `Geometry.Parameterization.Harmonic`).
- No new distortion *metric* — the reporting metrics stay in `Geometry.Parameterization.Diagnostics` (`GEOM-018`); this seam computes the per-element energy/gradient the optimizer descends, and must agree numerically with the diagnostics symmetric-Dirichlet value on shared inputs.
- No GPU or optimized backend; deterministic CPU numerics only. No Eigen types on the module surface.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- Today there is **no** exported parameterization-optimization seam. The symmetric-Dirichlet energy exists only as a *reported* per-face diagnostic in `Geometry.Parameterization.Diagnostics`; there is no shared local rotation fit, no PSD Hessian/proxy assembly, and no flip-barrier line search. Each of ARAP and SLIM would otherwise re-derive them privately.
- Present consumers that justify the seam (P1 second-caller rule): ARAP parameterization (`METHOD-021`) needs the per-triangle local rotation fit and the cotangent-weighted global proxy; SLIM (`METHOD-022`) needs the same reference-Jacobian proxy plus the symmetric-Dirichlet energy/gradient and the injective (flip-free) line search; the optimized progressive backend (`METHOD-025`) reuses all three. Three present callers, one core.
- Reuses `Geometry.Linalg` for the 2×2/3×2 SVD and polar decomposition behind geometry-owned APIs (no Eigen on the surface), `Geometry.Sparse::SparseLDLT` for the SPD proxy solve (the `GEOM-020` seam ARAP/Harmonic already use), and `Geometry.HalfedgeMesh` face traversal for per-triangle assembly. The signed-area/flip test reuses the same convention as `Geometry.Parameterization.Diagnostics`.

## Required changes
- [ ] Add module `Geometry.Parameterization.Optimize` (`.cppm` interface + `.cpp` implementation unit) in namespace `Geometry::Parameterization`, exposing only `std`/`glm`/scalar and geometry-owned types.
- [ ] Local step: `FitLocalRotations(mesh, uvs)` returning the per-face best-fit rotation (ARAP local step) and, for SLIM, the per-face reference Jacobian / target frame derived from the signed-SVD of the 2D deformation gradient, with reflection (flip) handling documented.
- [ ] Energy/gradient: `SymmetricDirichletEnergy(mesh, uvs)` and its per-vertex gradient, computed in `double`, agreeing with the `Geometry.Parameterization.Diagnostics` symmetric-Dirichlet mean/max on shared fixtures within a documented tolerance; degenerate/zero-area faces handled fail-closed (finite, flagged) rather than dividing by zero.
- [ ] Global proxy: assemble the cotangent-weighted SPD proxy system (ARAP global solve / SLIM reweighted proxy) as `Geometry.Sparse` triplets and solve with `SparseLDLT`, factored once per outer iteration where the pattern is fixed.
- [ ] Injective line search: `MaxInjectiveStep(mesh, uvs, direction)` returning the largest `t` in `[0, tMax]` such that no UV triangle changes signed-area sign (the SLIM/ARAP flip barrier), plus a backtracking helper that guarantees non-increasing energy; document the `t → 0` degenerate limit.
- [ ] Compute in `double` internally; expose `float`/scalar results. Fail-closed on non-triangle faces, non-finite inputs, and empty meshes (explicit status/`std::optional`, never NaN/Inf escape).
- [ ] Register `Geometry.Parameterization.Optimize.cppm` / `.cpp` in the existing `IntrinsicGeometry` module-library target lists in `src/geometry/CMakeLists.txt` (alphabetical placement; no new target — `glm`/`Eigen3` are already linked).

## Tests
- [ ] `tests/unit/geometry/Test.ParameterizationOptimize.cpp` with `unit;geometry` labels.
- [ ] Local rotation fit: on a rigidly rotated planar patch the fitted per-face rotation recovers the applied rotation; a pure scale returns identity rotation with the expected singular values.
- [ ] Energy agreement: `SymmetricDirichletEnergy` matches the `Geometry.Parameterization.Diagnostics` symmetric-Dirichlet value on identity and stretched-rectangle fixtures within tolerance; the isometric embedding attains the analytic minimum.
- [ ] Gradient check: finite-difference of the energy matches the analytic gradient on a small fixture within tolerance.
- [ ] Injective line search: on a step that would flip a triangle, `MaxInjectiveStep` returns a `t` strictly less than the flip distance and the resulting UVs have zero flipped elements; backtracking never increases energy.
- [ ] Determinism: identical `(mesh, uvs, direction)` produce bitwise-identical outputs across two runs and thread counts.
- [ ] Fail-closed: non-triangle faces, non-finite input, and empty meshes return explicit failure states with no NaN/Inf.

## Docs
- [ ] Interface documentation per `docs/architecture/geometry-api-style.md`: each kernel's closed form, the signed-SVD/flip convention, the SPD proxy definition, and the failure-state contract.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Record `METHOD-021`/`022`/`025` as the consumers in `tasks/backlog/geometry/README.md`, and note the seam under Pack 3 of `docs/architecture/parameterization-mapping-roadmap.md`.

## Acceptance criteria
- [ ] Public surface exposes only `std`/`glm`/scalar/geometry-owned types (no Eigen, no RHI).
- [ ] All listed tests pass in the default CPU gate.
- [ ] `METHOD-021` (ARAP) and `METHOD-022` (SLIM) can express their local step, global proxy, and (SLIM) flip-free line search against this surface without private optimization math.
- [ ] Layering check passes (`geometry -> core` only).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ParameterizationOptimize|Parameterization' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No solver outer-loop or per-method schedule in this task (named follow-up methods only).
- No new reported distortion metric (reuse `Geometry.Parameterization.Diagnostics`).
- No `std::rand` or global RNG state; no public Eigen types on the module interface.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed — this is a pure CPU numerics seam. GPU evaluation of the local step / proxy solve, if ever needed, opens with the family GPU backend (`METHOD-026`).

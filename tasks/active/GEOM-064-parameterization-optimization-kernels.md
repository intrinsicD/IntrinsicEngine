---
id: GEOM-064
theme: I
depends_on: [GEOM-018]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-LocalMerge"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-03T11:45:54Z"
contract_schema: 1
contracts: []
contract_review: "Pure geometry-owned numerical kernels do not consume or publish ECS element domains and do not bind a method into runtime, config, or UI."
maturity_target: CPUContracted
---
# GEOM-064 — Parameterization optimization kernels seam

## Status
- Active. The literature/reference contract below was frozen before source
  implementation on 2026-08-02.
- A draft module interface and implementation are checkpointed, but they are
  not registered in CMake, covered by the required tests, or evidence-eligible.
  Every actionable checkbox remains open and no `CPUContracted` claim is made.

## Goal
- Add a reusable geometry-owned kernel seam for iterative distortion-minimizing
  parameterization: precomputed triangle reference frames, per-triangle signed-
  SVD/rotation fitting, the orientation-aware symmetric-Dirichlet objective and
  gradient, ARAP/SLIM least-squares proxy assembly, and an injectivity-preserving
  maximum-step/line-search helper. ARAP, SLIM, and their evidence-gated
  follow-ups then share one tested numerical core instead of privately
  re-deriving local frames, proxy math, or flip barriers.

## Non-goals
- No solver control flow here — the local/global outer loop and per-method schedule stay in each method (`METHOD-021` ARAP, `METHOD-022` SLIM); this task ships only the stateless per-triangle and line-search primitives they consume.
- No dispatch surface (that is `GEOM-063`) and no boundary-mapping policy (that is `Geometry.Parameterization.Harmonic`).
- No new reporting metric. `Geometry.Parameterization.Diagnostics` remains the
  reporting owner. Its finite singular-value diagnostic on reflected faces is
  deliberately distinct from SLIM's orientation-aware optimization barrier,
  which rejects `det(J) <= epsilon`.
- No global-bijectivity claim. Positive triangle orientation proves local
  injectivity only; preventing boundary self-intersections is the distinct
  Smith--Schaefer boundary-barrier extension and remains out of scope.
- No GPU or optimized backend; deterministic CPU numerics only. No Eigen types on the module surface.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- Today there is **no** exported parameterization-optimization seam. The
  symmetric-Dirichlet value exists only as a reported per-face diagnostic;
  there is no shared reference-Jacobian preparation, proper-rotation fit,
  reweighted proxy assembly, or flip-barrier line search.
- Present consumers that justify the seam (P1 second-caller rule): ARAP parameterization (`METHOD-021`) needs the per-triangle local rotation fit and the cotangent-weighted global proxy; SLIM (`METHOD-022`) needs the same reference-Jacobian proxy plus the symmetric-Dirichlet energy/gradient and the injective (flip-free) line search; the optimized progressive backend (`METHOD-025`) reuses all three. Three present callers, one core.
- Reuses `Geometry.Linalg` for the 2x2 signed-SVD implementation behind
  geometry-owned records (no Eigen on the surface), `Geometry.Sparse` for the
  proxy normal matrix consumed later by `SparseLDLT`, and
  `Geometry.HalfedgeMesh` traversal. GEOM-064 assembles the system but does not
  own an iteration schedule or factorization lifecycle.

## Literature basis and reference boundary
- Original ARAP oracle: Liu, Zhang, Xu, Gotsman & Gortler,
  [A Local/Global Approach to Mesh Parameterization](https://www.cs.harvard.edu/~sjg/papers/arap.pdf)
  (SGP 2008). The reference energy is
  `sum_f A_f ||J_f - R_f||_F^2`; the proper rotation comes from a signed SVD,
  and the fixed global matrix may be prefactored by the method owner.
- Original SLIM oracle: Rabinovich, Poranne, Panozzo & Sorkine-Hornung,
  [Scalable Locally Injective Mappings](https://igl.ethz.ch/projects/slim/SLIM2017.pdf)
  (TOG 2017), cross-checked against the
  [authors' reference implementation](https://github.com/MichaelRabinovich/Scalable-Locally-Injective-Mappings).
  It uses the full area-weighted symmetric-Dirichlet objective
  `sum_f A_f (||J_f||_F^2 + ||J_f^-1||_F^2)`, a gradient-matching reweighted
  ARAP proxy, and a line search initialized at
  `min(1, 0.8 * alpha_max)` from a locally injective start.
- Maximum safe step and claim boundary: Smith & Schaefer,
  [Bijective Parameterization with Free Boundaries](https://people.engr.tamu.edu/schaefer/research/bijective.pdf)
  (SIGGRAPH 2015). Triangle degeneracy along a direction is a quadratic root;
  zero flips alone do not rule out boundary self-intersection.
- Later improvements are references, not silent substitutions:
  [Composite Majorization](https://shaharkov.github.io/projects/GOvCM.pdf)
  (Shtengel et al. 2017),
  [Isometry-Aware Preconditioning](https://diglib.eg.org/server/api/core/bitstreams/afd78ae2-3e1c-4d74-8ec2-74caf25f86a8/content)
  (Claici et al. 2017), and
  [ARAP Revisited](https://diglib.eg.org/bitstream/handle/10.1111/cgf14790/v42i6_41_14790.pdf)
  (Finnendahl et al. 2023) change the majorizer/preconditioner or ARAP
  discretization. They require separately named, benchmarked variants; this
  reference seam preserves the 2008/2017 objectives.

## Frozen numerical contract
- For each triangle, build a deterministic local source frame and
  `J = [uv1-uv0, uv2-uv0] * [x1-x0, x2-x0]^-1` in `double`.
- Fit the closest proper rotation with
  `R = U diag(1, det(U V^T)) V^T`; reflection is carried by the signed second
  singular value and is never accepted as a rotation.
- Report the full SLIM face objective (analytic minimum `4`) and its
  area-weighted sum. Also report `0.5 * D`, whose per-face mean/max must agree
  with the existing GEOM-018 diagnostics (analytic minimum `2`) on positive-
  orientation fixtures. A reflected/degenerate face is a barrier failure even
  though the reporting-only diagnostic remains finite for a reflection.
- Assemble ARAP with `W=I`. Assemble SLIM with the paper's
  `W = U [0.5 grad_D(Sigma) (Sigma-I)^-1]^(1/2) U^T`, using the analytic
  limit at `sigma -> 1`; the resulting normal equations are PSD, and the
  method-owned proximal/gauge term makes its solve SPD. Composite-majorization
  Hessians are not substituted.
- `MaxInjectiveStep` returns the first positive signed-area root, capped by the
  caller's `t_max`; the search helper starts strictly inside that boundary at
  `min(1, 0.8 * alpha_max)` and accepts only finite, positive-orientation,
  non-increasing symmetric-Dirichlet energy. The method task owns stopping and
  iteration counts.

## Required changes
- [ ] Add module `Geometry.Parameterization.Optimize` (`.cppm` interface + `.cpp` implementation unit) in namespace `Geometry::Parameterization`, exposing only `std`/`glm`/scalar and geometry-owned types.
- [ ] Add one plain precomputed reference record for face-storage-aligned vertex
      indices, local gradients, and positive 3D area. Every later kernel
      consumes it; deleted/non-triangle/degenerate/non-finite inputs fail
      closed during preparation.
- [ ] Local step: fit face-storage-aligned proper rotations, Jacobians, left
      singular frames, signed singular values, determinant, and validity from
      the frozen signed-SVD convention.
- [ ] Energy/gradient: evaluate the orientation-aware full symmetric-Dirichlet
      objective and analytic per-vertex gradient in `double`; expose the
      normalized per-face values needed to prove agreement with diagnostics.
- [ ] Global proxy: assemble ARAP and SLIM weighted least-squares normal
      matrices/RHS through `Geometry.Sparse::SparseBuilder`. A plain proxy
      record is the boundary; solving/factorization and outer-loop state remain
      in `METHOD-021`/`022`.
- [ ] Injective line search: compute the stable smallest positive quadratic
      root for every triangle, return the limiting face/step, and provide a
      bounded orientation-aware energy-decrease search with explicit
      no-descent/no-acceptable-step diagnostics.
- [ ] Compute in `double` internally; expose `float`/scalar results. Fail-closed on non-triangle faces, non-finite inputs, and empty meshes (explicit status/`std::optional`, never NaN/Inf escape).
- [ ] Register `Geometry.Parameterization.Optimize.cppm` / `.cpp` in the existing `IntrinsicGeometry` module-library target lists in `src/geometry/CMakeLists.txt` (alphabetical placement; no new target — `glm`/`Eigen3` are already linked).

## Tests
- [ ] `tests/unit/geometry/Test.ParameterizationOptimize.cpp` with `unit;geometry` labels.
- [ ] Local rotation fit: on a rigidly rotated planar patch the fitted per-face rotation recovers the applied rotation; a pure scale returns identity rotation with the expected singular values.
- [ ] Energy agreement: normalized per-face values match diagnostics on
      identity and stretched-rectangle fixtures; the full isometric objective
      attains `4` per face. A reflected face remains finite/countable in
      diagnostics but is rejected by the optimizer barrier.
- [ ] Gradient check: finite-difference of the energy matches the analytic gradient on a small fixture within tolerance.
- [ ] Proxy agreement: ARAP identity weights and SLIM analytic weights produce
      symmetric PSD normal matrices and gradient-matching directions on small
      fixtures; the proxy builder itself does not solve.
- [ ] Injective line search: on a step that would flip a triangle,
      `MaxInjectiveStep` returns the exact first root and the accepted safety-
      scaled step is strictly smaller, preserves positive orientation, and
      never increases energy.
- [ ] Determinism: identical `(reference, uvs, direction)` inputs produce
      bitwise-identical outputs across repeated calls; the reference kernels
      are serial and own no scheduler/thread-count axis.
- [ ] Fail-closed: non-triangle faces, non-finite input, and empty meshes return explicit failure states with no NaN/Inf.

## Docs
- [ ] Interface documentation per `docs/architecture/geometry-api-style.md`:
      each closed form, signed-SVD/orientation convention, proxy definition,
      line-search boundary, local-versus-global injectivity distinction, and
      failure-state contract.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Record `METHOD-021`/`022`/`025` as the consumers in `tasks/backlog/geometry/README.md`, and note the seam under Pack 3 of `docs/architecture/parameterization-mapping-roadmap.md`.

## Acceptance criteria
- [ ] Public surface exposes only `std`/`glm`/scalar/geometry-owned types (no Eigen, no RHI).
- [ ] All listed tests pass in the default CPU gate.
- [ ] `METHOD-021` (ARAP) and `METHOD-022` (SLIM) can express their local step,
      proxy assembly, and (SLIM) locally-injective line search against this
      surface without private per-triangle/proxy/barrier math.
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
- No factorization/solve, solver outer-loop, or per-method schedule in this
  task (named follow-up methods only).
- No new reported distortion metric (reuse `Geometry.Parameterization.Diagnostics`).
- No global-bijectivity or formal-global-convergence claim; no silent use of
  composite majorization, iARAP, AQP, or isometry-aware preconditioning.
- No `std::rand` or global RNG state; no public Eigen types on the module interface.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed — this is a pure CPU numerics seam. GPU evaluation of the local step / proxy solve, if ever needed, opens with the family GPU backend (`METHOD-026`).

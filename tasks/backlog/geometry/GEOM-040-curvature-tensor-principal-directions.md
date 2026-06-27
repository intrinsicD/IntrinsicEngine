---
id: GEOM-040
theme: none
depends_on: []
maturity_target: CPUContracted
---
# GEOM-040 — Mesh curvature tensor and principal directions

## Goal
- Extend the per-vertex curvature estimation in `Geometry.Curvature` to produce the full per-vertex 3×3 **curvature tensor** and the two **principal-curvature direction** fields (κ₁/κ₂ directions), which the engine currently lacks.
- Today the module computes only scalar magnitudes (`MeanCurvatureProperty` H, `GaussianCurvatureProperty` K, `MaxPrincipalCurvatureProperty` κ₁, `MinPrincipalCurvatureProperty` κ₂) and a single `MeanCurvatureNormalProperty`. This task adds tensor estimation (Taubin) eigen-decomposed into orthonormal tangent principal directions, published as new vertex properties, with κ₁/κ₂ magnitudes aligned to the directions they belong to.
- Keep every existing scalar curvature output bit-for-bit intact.

## Non-goals
- No curvature-based remeshing changes; `GEOM-043` owns the error-bounded sizing law and is the only consumer permitted to act on the direction fields for sizing.
- No GPU backend; this is a CPU-only contract under the default correctness gate.
- No UI / editor / visualization work; `UI-026` owns the editor curvature window and the direction-field publish for visualization (e.g. line-integral-convolution or hedgehog rendering).
- No change to the Meyer-et-al. scalar pipeline (`ComputeMeanCurvature`, `ComputeGaussianCurvature`, the H/K/κ₁/κ₂ derivation, or `MeanCurvatureNormalProperty`).
- No new public Eigen / external linear-algebra types on the module surface.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only). No runtime/graphics/ecs/assets/app coupling.
- Target module: `Geometry.Curvature`, interface `src/geometry/Geometry.HalfedgeMesh.Curvature.cppm`, implementation `src/geometry/Geometry.HalfedgeMesh.Curvature.cpp`, namespace `Geometry::Curvature`.
- `Geometry::Curvature::CurvatureField` currently exposes scalar magnitude properties plus `MeanCurvatureNormalProperty`; there is **zero** principal-direction support.
- Feature to port: `bcg_mesh_curvature_taubin` — per-vertex 3×3 curvature-tensor estimation (Taubin, "Estimating the tensor of curvature of a surface from a polyhedral approximation", ICCV 1995), eigen-decomposed into principal directions.
- The symmetric 3×3 eigensolver already exists as `Geometry::PCADetail::SymmetricEigen3` in `src/geometry/Geometry.PCA.cpp` (returns `Eigen3{ Eigenvalues, Eigenvectors[3] }`, gram-schmidt-orthonormalized), but it is an internal detail and is **not** currently exported from `Geometry.PCA.cppm` (which only exports `PCAResult` and `ToPCA`). This task must promote that solver to a reusable exported symbol so the curvature tensor can be decomposed without duplicating it.
- Adaptive remeshing already consumes the scalar curvature outputs; those consumers must keep compiling and producing identical results.

## Slice plan
- [x] Slice 1 — Promote the symmetric 3×3 eigensolver: export `Geometry::PCA::SymmetricEigen3` (and its `Eigen3` result struct) from `Geometry.PCA.cppm`, moving the existing implementation out of the anonymous/detail scope without changing its numerics, and rewire `ToPCA` to call the exported symbol. No behavioral change to PCA.
- [x] Slice 2 — Taubin tensor estimation: add `ComputeCurvatureTensor` and tensor→direction decomposition in `Geometry.Curvature`, publishing `v:principal_dir1` / `v:principal_dir2` and aligning κ₁/κ₂ magnitudes to their owning directions; extend `ComputeCurvature` to populate the new fields. Fail-closed on degenerate 1-rings and boundary vertices.

> Implementation note: the shared `Geometry::PCA::SymmetricEigen3` clamps its
> returned eigenvalues to non-negative (a PSD-covariance assumption). The
> curvature tensor is not PSD, so Slice 2 reuses the solver's eigenVECTORS and
> recovers the signed tangent eigenvalues from the tensor via the Rayleigh
> quotient `λ = vᵀ M v`. This keeps PCA numerics untouched while giving correct
> negative curvatures.

## Required changes
- [ ] In `src/geometry/Geometry.PCA.cppm`: export the symmetric eigensolver result struct and function as `Geometry::PCA::Eigen3` and `[[nodiscard]] Eigen3 SymmetricEigen3(const glm::dmat3& symmetricCovariance)` (or the existing six-component signature, preserved exactly). Keep the body in `src/geometry/Geometry.PCA.cpp`; only the small struct and the declaration live in the `.cppm`.
- [ ] In `src/geometry/Geometry.PCA.cpp`: relocate the current `PCADetail::SymmetricEigen3` body to define the now-exported `Geometry::PCA::SymmetricEigen3`, and update `ToPCA` to call it. Do not alter the eigenvalue/eigenvector math, the gram-schmidt re-orthonormalization, or the non-negative clamping.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Curvature.cppm`: extend `struct CurvatureField` with the new published direction fields:
  - [ ] `VertexProperty<glm::vec3> PrincipalDir1Property{};` — unit tangent direction of κ₁ (max principal curvature), published as `v:principal_dir1`.
  - [ ] `VertexProperty<glm::vec3> PrincipalDir2Property{};` — unit tangent direction of κ₂ (min principal curvature), published as `v:principal_dir2`.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Curvature.cppm`: declare a new entry point `[[nodiscard]] std::optional<CurvatureTensorResult> ComputeCurvatureTensor(HalfedgeMesh::Mesh& mesh);` and a small `struct CurvatureTensorResult { VertexProperty<glm::vec3> PrincipalDir1Property; VertexProperty<glm::vec3> PrincipalDir2Property; VertexProperty<double> MaxPrincipalCurvatureProperty; VertexProperty<double> MinPrincipalCurvatureProperty; };`. Keep declarations and doc-comments only in the interface; no non-trivial bodies.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Curvature.cpp`: implement the Taubin per-vertex 3×3 tensor estimator over each vertex 1-ring:
  - [ ] Estimate (or reuse) the vertex normal n_i.
  - [ ] For each edge (i,j) in the 1-ring, form the tangent direction T_ij = normalize((I − n_iⁱnᵢᵀ)(x_j − x_i)), the directional curvature κ_ij = 2 nᵢᵀ(x_j − x_i) / ‖x_j − x_i‖², and an area-derived weight w_ij (incident triangle areas), accumulating the symmetric tensor M_i = Σ_j w_ij κ_ij T_ij T_ijᵀ with Σ_j w_ij = 1.
  - [ ] Decompose M_i with the exported `Geometry::PCA::SymmetricEigen3`; the eigenvector closest to n_i (smallest |eigenvalue contribution along normal|) is discarded, and the remaining two tangent eigenvectors become the principal directions.
  - [ ] Recover κ₁ = 3λ_a − λ_b and κ₂ = 3λ_b − λ_a from the two tangent eigenvalues per Taubin, and assign each magnitude to the eigenvector it came from so `PrincipalDir1Property` is the direction of `MaxPrincipalCurvatureProperty`.
  - [ ] Normalize the two output directions to unit length and project them onto the tangent plane so they are orthonormal and tangent.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Curvature.cpp`: extend `ComputeCurvature` to additionally populate `PrincipalDir1Property` / `PrincipalDir2Property` in the returned `CurvatureField` while leaving the existing scalar fields and `MeanCurvatureNormalProperty` unchanged.
- [ ] Fail-closed handling (no asserts, no NaNs, explicit diagnostics per GEOM-005/GEOM-007): for a flat 1-ring (tensor numerically zero), a boundary vertex (open 1-ring), or a zero-area 1-ring, write deterministic sentinel directions (zero vec3) and leave κ₁/κ₂ as their scalar-derived values; for empty meshes / meshes with no faces return `nullopt` from `ComputeCurvatureTensor`, matching the existing `ComputeMeanCurvature` contract.
- [ ] Register no new module library and add no new CTest label; the new test file reuses the existing `unit;geometry` labels.

## Tests
- [ ] Add `tests/unit/geometry/Test.CurvatureTensor.cpp` with CTest labels `unit;geometry` (do not introduce new labels).
- [ ] Sphere (radius R): principal curvatures are equal (κ₁ ≈ κ₂ ≈ 1/R) and the tensor is isotropic; directions are any orthonormal tangent pair — assert isotropy (|κ₁ − κ₂| within tolerance) and that both directions are tangent and orthonormal.
- [ ] Cylinder (radius R, axis a): one principal curvature ≈ 0 and one ≈ 1/R; the zero-curvature direction is parallel to the axis a and the 1/R direction is circumferential, each within angular tolerance.
- [ ] Saddle (e.g. z = x² − y² near origin): the two principal curvatures have opposite signs and the principal directions are orthogonal and align with the x/y axes within tolerance.
- [ ] Orthonormality + tangency: for every interior vertex on the analytic fixtures, ‖dir1‖ ≈ 1, ‖dir2‖ ≈ 1, dir1·dir2 ≈ 0, and both dirs ⟂ vertex normal.
- [ ] Determinism: running `ComputeCurvatureTensor` twice on the same mesh yields bitwise-identical direction and magnitude properties.
- [ ] Scalar-output preservation: on a shared fixture, `ComputeCurvature` produces H/K/κ₁/κ₂/`MeanCurvatureNormal` identical to the pre-change baseline.
- [ ] Degenerate fail-closed: flat-region vertex, boundary vertex, and zero-area 1-ring each yield the documented sentinel (zero) directions with no NaN/Inf; empty mesh and no-face mesh yield `nullopt`.

## Docs
- [ ] Update `docs/architecture/geometry.md` to describe the new curvature-tensor / principal-direction outputs and the `v:principal_dir1` / `v:principal_dir2` property names.
- [ ] Note the promotion of `Geometry::PCA::SymmetricEigen3` to an exported reusable symbol in the same section (or in the PCA note).
- [ ] Regenerate `docs/api/generated/module_inventory.md` via `tools/repo/generate_module_inventory.py`.
- [ ] Record the Taubin tensor formulation and the degenerate fail-closed policy in `docs/methods/numerical-robustness-policy.md`.

## Acceptance criteria
- [ ] `Geometry.Curvature` publishes `v:principal_dir1` and `v:principal_dir2` and exposes `ComputeCurvatureTensor`; `ComputeCurvature` populates the new fields.
- [ ] `Geometry::PCA::SymmetricEigen3` is exported from `Geometry.PCA.cppm` and is the single solver used by both `ToPCA` and the new tensor decomposition (no duplicated eigensolver).
- [ ] Sphere isotropy, cylinder axis-alignment, and saddle opposite-sign/orthogonal tests pass within their documented tolerances.
- [ ] On all analytic fixtures, interior-vertex principal directions are unit-length, mutually orthogonal, and tangent to the surface within tolerance.
- [ ] `ComputeCurvatureTensor` is deterministic (identical output across repeated runs).
- [ ] Existing scalar curvature outputs are unchanged (regression test passes).
- [ ] Degenerate inputs fail closed with sentinel directions or `nullopt`, never NaN/Inf, and no asserts fire.
- [ ] No public Eigen / external linear-algebra types appear in `Geometry.Curvature.cppm` or `Geometry.PCA.cppm`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CurvatureTensor|Curvature|PCA' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No curvature-based remeshing / sizing-law changes (owned by `GEOM-043`).
- No UI, editor, or visualization changes (owned by `UI-026`).
- No change to the existing scalar curvature math (H/K/κ₁/κ₂ derivation, `MeanCurvatureNormalProperty`) or to PCA numerics.
- No new public Eigen / external linear-algebra types on any module surface.
- No GPU / renderer / runtime / ECS / assets / platform / app dependencies introduced into `geometry`.
- No mixing of mechanical file moves with semantic refactors (the `SymmetricEigen3` promotion in Slice 1 is a pure move + export, kept distinct from the Taubin logic in Slice 2).
- No introduction of unrelated feature work.
- No new CTest label unless `tests/README.md` and `tests/CMakeLists.txt` are updated in the same change.
- No performance-improvement claims without a baseline comparison.

## Maturity
- Target: `CPUContracted` — pure CPU geometry with deterministic, fail-closed contracts under the default correctness gate.
- Stop-state pin: closes at `CPUContracted` once the tensor/direction fields are published, the analytic-surface tests pass, and degenerate inputs fail closed. No `Operational` or `ParityProven` follow-up is owed here; there is no backend seam (the eigensolver is reused, not re-implemented), and downstream consumption is owned by `GEOM-043` (remeshing) and `UI-026` (visualization).

- Closure: no `Operational` follow-up is owed for this task.

---
id: GEOM-041
theme: none
depends_on: []
maturity_target: CPUContracted
---

# GEOM-041 — FEM Laplacian mass/stiffness variants and edge-weight modes

## Goal

- [ ] Extend the discrete Laplacian in `Geometry.HalfedgeMesh.DEC` to support the FEM stiffness/mass-mode family and additional edge-weight modes the engine currently lacks, while keeping the existing Cotan + HeatKernel weights and DEC diagnostics (`AnalyzeLaplacian`) intact.
- [ ] Add the stiffness (edge-weight) modes: Graph (combinatorial), Cotan (existing), Fujiwara (`w = 1/length`), and ModifiedNormal (`w = cot(e) * |n(v_from) . n(v_to)|`, feature-aware).
- [ ] Add the mass (Hodge0 / vertex-mass) modes: Sum/lumped (sum of incident diagonal area), Barycentric, Voronoi (existing mixed-Voronoi), and Galerkin (full consistent mass matrix).
- [ ] Add a standalone clamped per-halfedge cotan accessor (Heron / metric form with magnitude clamping) in `Geometry.HalfedgeMesh.Utils`, returning a `HalfedgeProperty<double>`.

## Non-goals

- No new linear solvers; this task only assembles operators (`Geometry.LinearSolver` and `Geometry.Sparse` are consumed, not extended).
- No GPU backend or GPU assembly path.
- No changes to parameterization, smoothing, curvature, geodesic, or heat-method behavior beyond making the new modes selectable; existing default modes must produce byte-for-byte identical results.
- No new public Laplacian assembly entry points beyond the additive enum cases and the standalone halfedge-cotan accessor.

## Context

- `Geometry.HalfedgeMesh.DEC.cppm` currently exposes `EdgeWeightMode` as only `{ Cotan = 0, HeatKernel = 1 }`, an `EdgeWeightConfig` carrying `Mode` plus a HeatKernel time parameter, and a `DECOperators` struct whose `Hodge0` diagonal is mixed-Voronoi only.
- Stiffness (edge weighting) and mass (`Hodge0`) are already separated in the assembly, but there are no FEM mode variants, no Fujiwara/ModifiedNormal weights, and no Galerkin consistent mass matrix.
- `Geometry.HalfedgeMesh.Utils.cppm` provides `Cotan(u, v)`, `EdgeCotanWeight(mesh, e)`, and `ComputeCotanLaplacian(...)`, but no standalone clamped per-halfedge cotan accessor and no Heron/metric form with magnitude clamping.
- `AnalyzeLaplacian(...)` and `LaplacianDiagnostics` in `Geometry.HalfedgeMesh.DEC.cppm` are the canonical DEC diagnostics and must keep working unchanged for every new mode.
- Per GEOM-005 and GEOM-007, all new weights/areas must be deterministic, fail-closed on degenerate/empty/non-finite input with explicit diagnostics (no asserts, no NaNs), and use the established tolerance policy for clamping.

## Slice plan

- [x] Slice 1 — Stiffness modes: extend `EdgeWeightMode` with `Graph`, `Fujiwara`, `ModifiedNormal`; wire each into stiffness assembly; row-sum and symmetry tests pass.
- [x] Slice 2 — Standalone clamped per-halfedge cotan accessor in `Geometry.HalfedgeMesh.Utils` (Heron/metric form, magnitude clamping) returning a `HalfedgeProperty<double>`; parity test against `EdgeCotanWeight`.
- [ ] Slice 3 — Mass modes: add the mass-mode enum (`Sum`/lumped, `Barycentric`, `Voronoi`, `Galerkin`) and consistent (Galerkin) mass assembly alongside the existing diagonal `Hodge0`; SPD + row-sum-equals-lumped tests pass.
- [ ] Slice 4 — Diagnostics & docs: confirm `AnalyzeLaplacian` covers all modes, update DEC docs, finalize fail-closed diagnostics.

> Progress: Slices 1-2 landed (stiffness modes + clamped halfedge-cotan accessor)
> with row-sum/symmetry, weight-parity, clamp-engages, and fail-closed tests in
> `tests/unit/geometry/Test_DEC.cpp`. Slices 3-4 (mass-mode family incl. the
> Galerkin consistent mass matrix on `DECOperators`, plus final diagnostics
> coverage) remain open.

## Required changes

- [ ] In `src/geometry/Geometry.HalfedgeMesh.DEC.cppm`: extend `enum class EdgeWeightMode : std::uint8_t` with `Graph`, `Fujiwara`, and `ModifiedNormal` cases (preserving `Cotan = 0`, `HeatKernel = 1` numeric values), and document each weight formula in the header comment block.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.DEC.cppm`: add a `MassMode` (vertex-mass) `enum class : std::uint8_t` with `Sum` (lumped), `Barycentric`, `Voronoi`, and `Galerkin` cases; add a `MassMode` field to the assembly config (default `Voronoi` to preserve current behavior) and, where Galerkin is requested, a consistent (non-diagonal) mass matrix member on `DECOperators` (e.g. a `SparseMatrix ConsistentMass`) alongside the existing diagonal `Hodge0`.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.DEC.cppm`: extend `EdgeWeightConfig` (or add a parallel config) so `ModifiedNormal` can reference per-vertex normals and a feature-weighting term; declare any new exported assembly overloads as thin signatures only (bodies live in the `.cpp`).
- [ ] In `src/geometry/Geometry.HalfedgeMesh.DEC.cpp`: implement stiffness assembly for `Graph` (`w = 1`), `Fujiwara` (`w = 1/length`, fail-closed on zero/non-finite length), and `ModifiedNormal` (`w = cot(e) * |n(v_from) . n(v_to)|`, using per-vertex normals), reusing the clamped halfedge cotan from `Utils` for the cotan term.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.DEC.cpp`: implement mass assembly for `Sum` (lumped = sum of incident diagonal area), `Barycentric` (one-third incident triangle area), `Voronoi` (existing mixed-Voronoi, refactored to share area helpers), and `Galerkin` (full consistent per-element mass matrix), ensuring the diagonal `Hodge0` for non-Galerkin modes and the consistent matrix for Galerkin are both populated deterministically.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.DEC.cpp`: ensure `AnalyzeLaplacian` / `LaplacianDiagnostics` remain valid for every new stiffness mode (row-sum and symmetry diagnostics computed against the assembled stiffness regardless of mode).
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm`: declare a standalone clamped per-halfedge cotan accessor returning a `HalfedgeProperty<double>` (e.g. `HalfedgeProperty<double> ClampedHalfedgeCotan(HalfedgeMesh::Mesh& mesh, double maxMagnitude = <policy default>)`), documented as Heron/metric form with explicit magnitude clamping.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Utils.cpp`: implement the clamped halfedge cotan via the Heron/metric triangle form, clamp the magnitude to the policy bound, fail-closed on degenerate (zero-area / non-finite) triangles with an explicit diagnostic, and write the result into the returned `HalfedgeProperty<double>`.
- [ ] Wire degenerate / empty / non-finite handling into all new code paths per GEOM-005/GEOM-007: explicit diagnostics, no asserts, no NaNs propagated into the assembled operator.

## Tests

- [ ] In `tests/unit/geometry/Test_DEC.cpp`: for each stiffness mode (`Graph`, `Cotan`, `Fujiwara`, `ModifiedNormal`) assert the stiffness row-sums are approximately zero within tolerance on a closed test mesh.
- [ ] In `tests/unit/geometry/Test_DEC.cpp`: assert each stiffness matrix is symmetric (`L == L^T` within tolerance) for every mode.
- [ ] In `tests/unit/geometry/Test_DEC.cpp`: assert the Galerkin consistent mass matrix is SPD (symmetric, positive eigenvalues / positive diagonal-dominant test) and that its row-sums equal the lumped (`Sum`) masses within tolerance.
- [ ] In `tests/unit/geometry/Test_DEC.cpp`: on a small hand-constructed mesh, assert `Fujiwara` weights equal hand-computed `1/length` values and `ModifiedNormal` weights equal hand-computed `cot(e) * |n.n|` values within tolerance.
- [ ] In `tests/unit/geometry/Test_DEC.cpp` (or the Utils test): assert the clamped per-halfedge cotan matches `EdgeCotanWeight` for the same edge within tolerance on non-degenerate triangles, and that the magnitude clamp engages on a near-degenerate sliver triangle.
- [ ] In `tests/unit/geometry/Test_DEC.cpp`: assert all new modes fail closed (explicit diagnostic, no NaN in the operator) on empty meshes, zero-length edges, and non-finite vertex positions.
- [ ] Tests carry the existing `unit;geometry` label; no new CTest labels are introduced.

## Docs

- [ ] Update the DEC header doc comments in `src/geometry/Geometry.HalfedgeMesh.DEC.cppm` to describe each stiffness mode and mass mode with its formula and the lumped-vs-consistent mass distinction.
- [ ] Update any DEC / Laplacian section under `docs/` that enumerates edge-weight modes to include `Graph`, `Fujiwara`, `ModifiedNormal`, and the mass-mode family.
- [ ] Regenerate `docs/api/generated/module_inventory.md` so the new exported symbols (mass-mode enum, consistent-mass member, clamped halfedge-cotan accessor) appear.

## Acceptance criteria

- [ ] `EdgeWeightMode` includes `Graph`, `Cotan`, `Fujiwara`, `ModifiedNormal`; `Cotan = 0` and `HeatKernel = 1` numeric values are unchanged.
- [ ] A `MassMode` enum with `Sum`, `Barycentric`, `Voronoi`, `Galerkin` exists and is selectable; the default reproduces the previous mixed-Voronoi `Hodge0` exactly.
- [ ] Every stiffness mode yields a symmetric matrix with row-sums approximately zero within the test tolerance; `AnalyzeLaplacian` reports valid diagnostics for each.
- [ ] The Galerkin consistent mass matrix is SPD and its row-sums equal the lumped (`Sum`) masses within tolerance.
- [ ] The standalone clamped per-halfedge cotan accessor returns a `HalfedgeProperty<double>` matching `EdgeCotanWeight` within tolerance on non-degenerate triangles and clamps on slivers.
- [ ] All new code paths fail closed on empty/degenerate/non-finite input with explicit diagnostics and never emit NaNs into the assembled operator.
- [ ] No `src/geometry/*` file imports assets/runtime/graphics/rhi/ecs/app; layering and task-policy checks pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'DEC|Laplacian|HalfedgeMesh.*Utils' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves with the semantic refactor in this task.
- Introducing unrelated feature work (new solvers, smoothing/parameterization behavior changes, GPU paths).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into `src/geometry/*`.
- Changing the numeric values of existing `EdgeWeightMode` cases or altering default Voronoi `Hodge0` output.
- Claiming performance improvements without a baseline comparison.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.

## Maturity

- Stop-state pin: CPUContracted. All new stiffness modes, mass modes, and the clamped halfedge-cotan accessor are fully implemented on the CPU with deterministic, fail-closed behavior and covered by the row-sum / symmetry / SPD / hand-computed / parity / degenerate tests above. No GPU backend, no parity-vs-optimized backend, and no solver work are in scope; those would be separate follow-on tasks.

---
id: METHOD-003
theme: I
depends_on: [GEOM-023]
---
# METHOD-003 — Closest Point Method PDE solver reference backend

## Goal
- Add a CPU reference backend for solving scalar / vector PDEs on a surface given only a closest-point query and a narrow band of grid samples — the Closest Point Method (CPM). Enables surface PDEs on inputs that are not halfedge meshes (point clouds, level sets, parametric closest-point oracles) and complements the DEC pipeline.

## Non-goals
- No GPU backend.
- No replacement of existing DEC-based PDE solvers; CPM is a peer solver covering inputs DEC cannot handle.
- No general grid PDE framework — this task is scoped to the closest-point embedding method only.
- No neural / learned variants.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/closest_point_pde/`.
- Paper: see Variants below.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #2.
- Reuses `Geometry.Grid`, `Geometry.SDF`, `Geometry.KDTree` / `Geometry.BVH` (closest-point oracle), the CSR builder / CG iterative solver from retired [`GEOM-008`](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md), the direct sparse SPD factorization (LDLT/LLT) seam from retired [`GEOM-020`](../../archive/GEOM-020-sparse-direct-factorization-seam.md), and the non-symmetric BiCGSTAB seam from retired [`GEOM-023`](../../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md). The `L_band` operator assembled in Step 5 is non-symmetric for the closest-point-extension formulation, so the practical solver path is `Geometry.Sparse::SparseBiCGSTAB`. GMRES remains a possible follow-up only if a concrete CPM slice proves BiCGSTAB insufficient.
- Symmetric-domain-views work in [`GEOM-012`](../../archive/GEOM-012-symmetric-domain-views-property-sharing.md) is a soft prerequisite: this method must accept a `ClosestPoint` interface backed by any of: halfedge mesh, point cloud, or implicit SDF.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become optional capability flags or follow-up tasks.

- [x] **A — CPM with interior boundary conditions (King, Berger-Vergiat, Macdonald, Wong; TOG 2024, arXiv:2305.04711).** Most general; handles Dirichlet on interior curves on the surface (diffusion curves, harmonic maps, tangent-vector field design, reaction-diffusion textures). **Selected as the default; its non-symmetric solver need is satisfied by retired `GEOM-023`.**
- [ ] **B — Classic CPM (Ruuth & Merriman 2008, Macdonald & Ruuth 2009).** Simpler, no interior BC support; smaller code surface. Pick if interior BC are not needed in year-1 scope.
- [ ] **C — Generalized MLS for vector-valued PDEs on unknown manifolds (Liang et al., arXiv:2406.12210).** Pure point-cloud solver, no embedding grid. Pick only if point-cloud-only operation is the primary use case.

Default recommendation: **A**.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/closest_point_pde/`.
- [ ] Fill `method.yaml` (`id: geometry.closest_point_pde`, paper citation, metrics: `pde_residual_l2`, `convergence_order`, `runtime_ms`).
- [ ] Fill `paper.md` per template.

### Public API in `src/geometry`
- [ ] Add module `Geometry.ClosestPointPDE` in `src/geometry/Geometry.ClosestPointPDE.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::ClosestPointPDE {
    struct ClosestPointOracle {
      // Returns nearest surface point + outward normal for any sample x in R^3.
      virtual glm::dvec3 ClosestPoint(glm::dvec3 x) const = 0;
      virtual glm::dvec3 Normal(glm::dvec3 surface_point) const = 0;
    };
    struct Input {
      const ClosestPointOracle& oracle;
      AABB band_bounds;              // bounding box of the surface
      double grid_spacing;           // h
      std::span<const BoundaryCurve> interior_dirichlet;   // variant A only
    };
    enum class Equation { Laplace, Poisson, Heat, ReactionDiffusion };
    struct Params { Equation equation; double t_final = 0.0; /* ... */ };
    struct Result {
      Geometry::Grid<double> band_field;   // narrow-band values
      Diagnostics diagnostics;
    };
    Core::Expected<Result> Solve(const Input&, const Params&);
  }
  ```
- [ ] Register module in `src/geometry/CMakeLists.txt`; do **not** add to `Geometry.cppm` umbrella initially (advanced numerical surface).

### Implementation steps
- [ ] Step 1: build a narrow band around the surface using `Geometry.Grid` + the closest-point oracle (bandwidth ≥ `(p+1)/2 * h` for finite-difference order `p`, plus interpolation support).
- [ ] Step 2: assemble Laplacian on the grid using standard 7-point stencil (3D) restricted to the band.
- [ ] Step 3: implement the closest-point extension operator (barycentric / Lagrange interpolation at closest points).
- [ ] Step 4: variant A — partition stencils across interior BC curves following §3 of arXiv:2305.04711.
- [ ] Step 5: solve `L_band X = b`. The variant-A closest-point-extension operator is non-symmetric: use `Geometry.Sparse::SparseBiCGSTAB` from retired [`GEOM-023`](../../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md). Where a sub-step produces an SPD system, the LDLT path from retired [`GEOM-020`](../../archive/GEOM-020-sparse-direct-factorization-seam.md) or the CG path from retired [`GEOM-008`](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md) remains preferable; record the per-step solver choice in the slice plan.

### Closest-point oracle adapters
- [ ] Add adapter `Geometry::ClosestPointPDE::OracleFromHalfedgeMesh` using existing `Geometry.BVH`.
- [ ] Add adapter `Geometry::ClosestPointPDE::OracleFromPointCloud` using `Geometry.KDTree` + `Geometry.PointCloud.Normals`.
- [ ] Add adapter `Geometry::ClosestPointPDE::OracleFromSDF` using `Geometry.SDF` gradient.

## Tests
- [ ] `tests/unit/geometry/Test.ClosestPointPDE.cpp`.
- [ ] Analytic Laplace–Beltrami eigenfunction on a sphere — verify recovered eigenvalues to ≥ 2nd-order convergence in `h`.
- [ ] Heat diffusion on a torus — compare equilibrium against analytic constant.
- [ ] Interior Dirichlet diffusion curve on a flat patch — match closed-form linear field between two parallel curves (variant A only).
- [ ] Oracle parity: same surface fed through mesh / point-cloud / SDF oracles → solution agrees within `O(h)`.
- [ ] Determinism with fixed seed and fixed band order.

## Docs
- [ ] `methods/geometry/closest_point_pde/README.md`.
- [ ] Add a section in `docs/architecture/geometry.md` (or roadmap doc) introducing the closest-point oracle abstraction as the canonical cross-domain PDE seam.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] All three oracle adapters compile and pass the parity test.
- [ ] Convergence test demonstrates the documented order in `h`.
- [ ] No Eigen types leak through the public API.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ClosestPointPDE|Grid|SDF' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No neural / learned solver in this task.
- No GPU backend before parity tests.
- No dependency on graphics/runtime/ECS.
- No coupling to a specific surface representation in the public API — must go through `ClosestPointOracle`.

## Maturity
- Target: `CPUContracted`. The CPU reference backend is the correctness oracle for any later optimized/GPU backend.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU backends open as separate method tasks per `AGENTS.md` §6 once reference parity exists.

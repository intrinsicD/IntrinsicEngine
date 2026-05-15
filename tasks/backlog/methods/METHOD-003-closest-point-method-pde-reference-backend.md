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
- Reuses `Geometry.Grid`, `Geometry.SDF`, `Geometry.KDTree` / `Geometry.BVH` (closest-point oracle), and the sparse-solver seam from [`GEOM-008`](../geometry/GEOM-008-linear-algebra-solver-infrastructure.md).
- **Hard dependency:** [`GEOM-015`](../geometry/GEOM-015-common-method-package-infrastructure.md) for `Geometry::ClosestPointOracle`, `Geometry::PDE::BoundaryConditions`, `Geometry::Diagnostics`.
- Symmetric-domain-views work in [`GEOM-012`](../geometry/GEOM-012-symmetric-domain-views-property-sharing.md) is a soft prerequisite: this method must accept a `ClosestPoint` interface backed by any of: halfedge mesh, point cloud, or implicit SDF.

## Shared infrastructure consumed / extracted

This task **consumes** (depends on) from [`GEOM-015`](../geometry/GEOM-015-common-method-package-infrastructure.md):

- `Geometry::ClosestPointOracle` — the canonical input abstraction; this task does **not** redefine its own oracle interface.
- `Geometry::PDE::BoundaryConditions::ScalarBC` (variant A) — for interior Dirichlet curves.
- `Geometry::Diagnostics` — used as `Result::diagnostics`.

This task **may extract** (if not already in `GEOM-015`):

- `Geometry::Grid::NarrowBand` — narrow-band grid view (a band-of-cells subset of a `Geometry::Grid`). If a clean abstraction emerges during implementation, promote it into `Geometry.Grid` as a public type rather than keeping it private to this method package.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become optional capability flags or follow-up tasks.

- [ ] **A — CPM with interior boundary conditions (King, Berger-Vergiat, Macdonald, Wong; TOG 2024, arXiv:2305.04711).** Most general; handles Dirichlet on interior curves on the surface (diffusion curves, harmonic maps, tangent-vector field design, reaction-diffusion textures). Recommended default.
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
- [ ] Public surface (sketch) — note that the oracle type is **imported** from `Geometry::ClosestPointOracle` (GEOM-015), not redefined:
  ```cpp
  namespace Geometry::ClosestPointPDE {
    struct Input {
      const Geometry::ClosestPointOracle& oracle;     // from GEOM-015
      AABB band_bounds;                                // bounding box of the surface
      double grid_spacing;                             // h
      // Interior Dirichlet curves projected onto the surface via oracle.Query().
      std::span<const std::vector<glm::dvec3>> interior_dirichlet_curves;  // variant A only
    };
    enum class Equation { Laplace, Poisson, Heat, ReactionDiffusion };
    struct Params {
      Equation equation;
      double t_final = 0.0;
      uint32_t fd_order = 2;          // finite-difference spatial order; band radius depends on this
      std::function<double(glm::dvec3)> source_term;   // optional Poisson RHS
    };
    struct Result {
      Geometry::Grid<double> band_field;   // narrow-band values; see narrow-band note below
      Geometry::Diagnostics diagnostics;
    };
    Core::Expected<Result> Solve(const Input&, const Params&);
  }
  ```
- [ ] Register module in `src/geometry/CMakeLists.txt`; do **not** add to `Geometry.cppm` umbrella initially (advanced numerical surface).
- [ ] **Narrow-band radius contract:** the band must include at least `ceil((p+1)/2) * h` cells around the surface for finite-difference order `p`, plus the closest-point interpolation stencil radius (typically 2 cells for tri-cubic). Document this in the module header as the precondition on `Input::band_bounds` vs the surface extent.
- [ ] **Interior BC projection:** the polyline samples in `interior_dirichlet_curves` may live anywhere in R^3; the implementation must project each sample to the surface via `oracle.Query()` and rasterise into the narrow band. Document this in the module header.
- [ ] **SDF normal note:** when using `OracleFromSDF`, `ClosestPointResult::normal` is the normalised gradient and is finite only away from the medial axis. The implementation must guard against NaN normals at sampled points coincident with the medial axis and surface them via `Diagnostics::degenerate_input_count`.

### Implementation steps
- [ ] Step 1: build a narrow band around the surface using `Geometry.Grid` + the closest-point oracle (bandwidth ≥ `(p+1)/2 * h` for finite-difference order `p`, plus interpolation support).
- [ ] Step 2: assemble Laplacian on the grid using standard 7-point stencil (3D) restricted to the band.
- [ ] Step 3: implement the closest-point extension operator (barycentric / Lagrange interpolation at closest points).
- [ ] Step 4: variant A — partition stencils across interior BC curves following §3 of arXiv:2305.04711.
- [ ] Step 5: solve `L_band X = b` via the LDLT / iterative path from `GEOM-008`.

### Closest-point oracle adapters

The oracle adapters live in `GEOM-015`, **not** in this method package. This task only consumes them:

- `Geometry::OracleFromHalfedgeMesh`
- `Geometry::OracleFromPointCloud`
- `Geometry::OracleFromSDF`

If any of these adapters needs an extension specific to PDE solving (e.g. cached gradient evaluation), promote the extension into `GEOM-015` rather than adding it here.

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

---
id: METHOD-003
theme: I
depends_on: [GEOM-023]
maturity_target: CPUContracted
---
# METHOD-003 — Closest Point Method PDE solver reference backend

## Goal
- Add a CPU reference backend for solving the selected scalar surface-PDE
  contract from a closest-point query and a narrow band of grid samples — the
  Closest Point Method (CPM). It covers inputs that are not halfedge meshes
  (point clouds, level sets, parametric closest-point oracles) and complements
  the DEC pipeline.

## Non-goals
- No GPU backend.
- No replacement of existing DEC-based PDE solvers; CPM is a peer solver covering inputs DEC cannot handle.
- No general grid PDE framework — this task is scoped to the closest-point embedding method only.
- No neural / learned variants.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/closest_point_pde/`.
- Paper: King, Berger-Vergiat, Macdonald & Wong, "The Closest Point
  Method for Surface PDEs with Interior Boundary Conditions", ACM TOG 2024.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #2.
- Reuses `Geometry.Grid`, `Geometry.SDF`, `Geometry.KDTree` / `Geometry.BVH` (closest-point oracle), the CSR builder / CG iterative solver from retired [`GEOM-008`](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md), the direct sparse SPD factorization (LDLT/LLT) seam from retired [`GEOM-020`](../../archive/GEOM-020-sparse-direct-factorization-seam.md), and the non-symmetric BiCGSTAB seam from retired [`GEOM-023`](../../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md). The `L_band` operator assembled in Step 5 is non-symmetric for the closest-point-extension formulation, so the practical solver path is `Geometry.Sparse::SparseBiCGSTAB`. GMRES remains a possible follow-up only if a concrete CPM slice proves BiCGSTAB insufficient.
- Symmetric-domain-views work in [`GEOM-012`](../../archive/GEOM-012-symmetric-domain-views-property-sharing.md) is a soft prerequisite: this method must accept a `ClosestPoint` interface backed by any of: halfedge mesh, point cloud, or implicit SDF.

## Variants and default selection

- **Selected — CPM with interior boundary conditions** (King,
  Berger-Vergiat, Macdonald, Wong; TOG 2024, arXiv:2305.04711). This task
  implements the scalar closest-point-extension formulation and its
  non-symmetric solve through retired `GEOM-023`.
- **Comparison baseline — classic CPM** (Ruuth & Merriman 2008; Macdonald &
  Ruuth 2009). Use only where needed to isolate the effect of interior
  boundary conditions; it is not a second public strategy.
- **Deferred — generalized MLS on unknown manifolds.** It has a different
  discretization and opens as its own method task if prioritized; it is not a
  capability flag in this task.

## Slice plan

- **Slice A — intake and contract.** Freeze the selected formulation,
  equations, input/output units, closest-point accuracy assumptions, boundary
  semantics, diagnostics, fixtures, tolerances, and benchmark identity before
  implementation.
- **Slice B — scalar CPU reference.** Implement the narrow-band operator and
  interior-Dirichlet path against one analytic oracle adapter, with
  deterministic ordering and fail-closed solver diagnostics.
- **Slice C — representation parity.** Add the mesh, point-cloud, and SDF
  adapters and prove that their approximation errors stay within separately
  declared bounds; do not force one tolerance across different oracle
  accuracies.
- **Slice D — evidence and docs.** Add the executable correctness smoke,
  schema-valid result JSON, limitations, and generated inventory.

## Right-sizing

- The query seam is justified by three present adapters. Keep it limited to
  closest-point/normal queries and do not introduce a general grid-PDE
  framework, solver registry, or representation service.
- Implement only the selected scalar formulation. Vector-valued generalized
  MLS and additional time integrators require separately evidenced callers.

## Backends

- Backend axis: deterministic `cpu_reference` only. Any optimized CPU or GPU
  implementation opens as a separate method task after this oracle and its
  convergence evidence exist.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/closest_point_pde/`.
- [ ] Fill `method.yaml` (`id: geometry.closest_point_pde`, paper citation, metrics: `pde_residual_l2`, `convergence_order`, `runtime_ms`).
- [ ] Fill `paper.md` with the objective, operator equations, assumptions,
      spatial/time units, boundary convention, expected convergence order,
      stopping rules, and failure-state taxonomy.

### Public API in `src/geometry`
- [ ] Add module `Geometry.ClosestPointPDE` in `src/geometry/Geometry.ClosestPointPDE.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::ClosestPointPDE {
    struct ClosestPointOracle {
      virtual ~ClosestPointOracle() = default;
      // Returns nearest surface point + outward normal for any sample x in R^3.
      virtual glm::dvec3 ClosestPoint(glm::dvec3 x) const = 0;
      virtual glm::dvec3 Normal(glm::dvec3 surface_point) const = 0;
    };
    struct Input {
      const ClosestPointOracle& oracle;
      AABB band_bounds;              // bounding box of the surface
      double grid_spacing;           // h
      std::span<const BoundaryCurve> interior_dirichlet; // selected formulation
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
- [ ] Step 4: for the selected interior-boundary formulation, partition
      stencils across interior BC curves following §3 of arXiv:2305.04711.
- [ ] Step 5: solve `L_band X = b`. The selected
      closest-point-extension operator is non-symmetric: use
      `Geometry.Sparse::SparseBiCGSTAB` from retired
      [`GEOM-023`](../../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md).
      Where a sub-step produces an SPD system, the LDLT path from retired
      [`GEOM-020`](../../archive/GEOM-020-sparse-direct-factorization-seam.md)
      or the CG path from retired
      [`GEOM-008`](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md)
      remains preferable; record the per-step solver choice in the slice plan.

### Closest-point oracle adapters
- [ ] Add adapter `Geometry::ClosestPointPDE::OracleFromHalfedgeMesh` using existing `Geometry.BVH`.
- [ ] Add adapter `Geometry::ClosestPointPDE::OracleFromPointCloud` using `Geometry.KDTree` + `Geometry.PointCloud.Normals`.
- [ ] Add adapter `Geometry::ClosestPointPDE::OracleFromSDF` using `Geometry.SDF` gradient.

### Benchmark
- [ ] Add an executable deterministic smoke with stable ID
      `geometry.closest_point_pde.reference.smoke`, a built-in analytic
      sphere/flat-patch dataset, `intent: correctness`, explicit warmup and
      measured iterations, and allowed manifest metrics `runtime_ms` and
      `quality_error_l2`. Put residuals, observed convergence order, band size,
      solver iterations, and oracle identity in result diagnostics.
- [ ] Emit schema-valid result JSON with `backend: cpu_reference`; the smoke
      uses no external dataset and makes no performance claim.

## Tests
- [ ] `tests/unit/geometry/Test.ClosestPointPDE.cpp`.
- [ ] Analytic Laplace–Beltrami eigenfunction on a sphere — verify recovered eigenvalues to ≥ 2nd-order convergence in `h`.
- [ ] Heat diffusion on a torus — compare equilibrium against analytic constant.
- [ ] Interior Dirichlet diffusion curve on a flat patch — match the
      closed-form linear field between two parallel curves.
- [ ] Oracle accuracy: each mesh/point-cloud/SDF adapter converges against the
      same analytic surface under its frozen approximation model; pairwise
      solution differences stay within the combined declared oracle bounds
      rather than an unjustified universal `O(h)` tolerance.
- [ ] Determinism with fixed seed and fixed band order.
- [ ] Freeze analytic error, residual, and convergence-order tolerances in the
      method contract before implementing the assertions; cover solver
      non-convergence, invalid bands, non-finite oracle output, and inconsistent
      boundary data as explicit failures.

## Docs
- [ ] `methods/geometry/closest_point_pde/README.md`.
- [ ] Add a section in `docs/architecture/geometry.md` (or roadmap doc) introducing the closest-point oracle abstraction as the canonical cross-domain PDE seam.
- [ ] Register the stable benchmark ID and its built-in dataset under
      `benchmarks/geometry/`; document that it is a correctness smoke, not a
      speed claim.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] CPM with interior boundary conditions is the sole public strategy; the
      classic CPM comparison remains internal.
- [ ] All three oracle adapters compile and pass the parity test.
- [ ] Convergence test demonstrates the documented order in `h`.
- [ ] The smoke manifest and emitted result validate, and the result reports
      residual/convergence diagnostics against the analytic CPU truth.
- [ ] No Eigen types leak through the public API.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'ClosestPointPDE|Grid|SDF|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No neural / learned solver in this task.
- No GPU backend before parity tests.
- No dependency on graphics/runtime/ECS.
- No coupling to a specific surface representation in the public API — must go through `ClosestPointOracle`.

## Maturity
- Target: `CPUContracted`. The CPU reference backend is the correctness oracle for any later optimized/GPU backend.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU backends open as separate method tasks per `AGENTS.md` §6 once reference parity exists.

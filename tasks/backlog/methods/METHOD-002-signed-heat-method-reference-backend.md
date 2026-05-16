# METHOD-002 — Signed Heat Method reference backend

## Goal
- Add a CPU reference backend that computes signed geodesic distance (and unsigned distance to broken / noisy boundary inputs) on triangle meshes and point clouds, mirroring the API style of the existing scalar heat method (`Geometry.HalfedgeMesh.Geodesic`) and vector heat method (`Geometry.HalfedgeMesh.VectorHeatMethod`).

## Non-goals
- No GPU backend.
- No optimized CPU backend until reference parity tests pass against the geometry-central reference.
- No replacement of the existing scalar/vector heat-method modules; this is a peer module, not a rewrite.
- No new IO formats; consumes existing halfedge mesh / point cloud containers.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry` (paper package).
- Method package: `methods/geometry/signed_heat/`.
- Paper(s): see Variants below.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #1, against gaps in [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md) (geodesics + intrinsic geometry pack; tolerant input for noisy / broken boundary data).
- Reuses the cotan Laplacian / mass matrix already assembled by `Geometry.HalfedgeMesh.DEC` and the upcoming general sparse-solver seam from [`GEOM-008`](../geometry/GEOM-008-linear-algebra-solver-infrastructure.md).
- Reference C++ implementation exists in geometry-central's `signed_heat_method` module and may be used as the parity oracle, not as a dependency.
- Pathfinder method per [`METHODS-001`](METHODS-001-signed-heat-pathfinder.md): the first method to be driven end-to-end through the `methods/` pipeline. When `GEOM-008` retires, promote this task to `tasks/active/` and start with the method-package scaffolding slice + Variant A reference implementation against the analytic disk test.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend** once CPU reference parity is demonstrated. Unmarked variants become optional comparison backends behind capability flags or remain out of scope.

- [x] **A — Signed Heat Method on surfaces (Feng & Crane, SIGGRAPH 2024).** Solves signed geodesic distance to a curve / region boundary on a surface mesh. Best fit for the existing halfedge mesh pipeline; reuses cotan-Laplace + mass assembly already present. **Selected as the default variant per `METHODS-001`.**
- [ ] **B — Signed Heat Method on point clouds (Feng & Crane, SIGGRAPH 2024).** Same algorithm with point-cloud Laplacian; only viable if point-cloud Laplacian assembly is added under [`GEOM-010`](../geometry/GEOM-010-point-cloud-algorithm-pack-roadmap.md).
- [ ] **C — Generalized signed distance in R^n (Feng & Crane).** Volumetric grid variant; only viable if `Geometry.Grid` gains a Laplacian assembly path.

Default recommendation: **A** (smallest reuse of existing engine code; B/C as follow-ups).

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/signed_heat/`.
- [ ] Fill `method.yaml` (`id: geometry.signed_heat`, status `proposed`, paper citation, backends, metrics: `signed_distance_l2`, `iso_offset_spacing_stddev`, `runtime_ms`).
- [ ] Fill `paper.md` per `methods/_template/paper.md`: claim decomposition, governing equations (heat diffusion of normals → unit gradient field → Poisson recovery of scalar), degeneracy behavior.
- [ ] Write `README.md` documenting backend identity (`cpu_reference`) and the selected default variant.

### Public API in `src/geometry`
- [ ] Add module `Geometry.HalfedgeMesh.SignedHeatMethod` in `src/geometry/Geometry.HalfedgeMesh.SignedHeatMethod.cppm` + `.cpp`.
- [ ] Mirror the namespace pattern used by `Geometry.HalfedgeMesh.VectorHeatMethod` (per `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` §1 naming policy follow-up).
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::SignedHeatMethod {
    struct Input {
      const HalfedgeMesh::Mesh& mesh;
      std::span<const HalfedgeMesh::HalfedgeHandle> boundary;  // broken curve as halfedges
      // or: std::span<const glm::vec3> polyline; // R^3 polyline projected to surface
    };
    struct Params { double t_smooth = -1.0; /* auto = mean edge length squared */
                    bool preserve_source_normals = true; };
    struct Result {
      std::vector<double> signed_distance;       // per-vertex
      Diagnostics diagnostics;                   // residuals, degeneracy counts
    };
    Core::Expected<Result> Solve(const Input&, const Params&);
  }
  ```
- [ ] Register the new module in `src/geometry/CMakeLists.txt` and re-export from `Geometry.cppm` only if it is part of the umbrella surface.

### Implementation
- [ ] Step 1: assemble cotan-Laplace `L`, lumped mass `M` via existing `Geometry.HalfedgeMesh.DEC` exports.
- [ ] Step 2: diffuse boundary normals for time `t = h^2` where `h = mean edge length`, using `(M + tL) X = X_0` solved by the LDLT path from `GEOM-008`.
- [ ] Step 3: normalize diffused vector field per vertex.
- [ ] Step 4: solve Poisson `L φ = div(X̂)` for the signed scalar, fixing one boundary vertex for the constant null space.
- [ ] All matrix assembly behind the geometry-owned Eigen adapter (no Eigen types in the public API).

## Tests
- [ ] Add `tests/unit/geometry/Test.SignedHeatMethod.cpp` (CTest labels `unit;geometry`).
- [ ] Analytic case: signed distance on a flat disk to a circular boundary — compare against closed form.
- [ ] Convergence: refine a sphere mesh and verify L2 error in signed distance decreases at the expected rate.
- [ ] Robustness: noisy / non-closed input curve (drop random boundary halfedges) — verify result remains finite and diagnostics flag the input as degenerate.
- [ ] Determinism: identical input + seed → bitwise identical output across two runs.

## Docs
- [ ] `methods/geometry/signed_heat/README.md` — method contract, backend status, known limitations.
- [ ] Append to `docs/api/generated/module_inventory.md` via `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [ ] Cross-link from `docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md` once landed.

## Acceptance criteria
- [ ] One variant marked as default; non-default variants either deferred to follow-up tasks or removed from scope.
- [ ] CPU reference produces signed distance within documented tolerance against analytic disk + sphere baselines.
- [ ] Diagnostics distinguish: non-convergent solve, degenerate input boundary, NaN propagation.
- [ ] Layering check passes (`geometry -> core` only; no Eigen leakage through `Geometry.cppm`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SignedHeat|Geodesic|VectorHeatMethod' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No GPU backend before reference parity.
- No replacement of `Geometry.HalfedgeMesh.Geodesic` or `Geometry.HalfedgeMesh.VectorHeatMethod`.
- No public Eigen types in `Geometry.HalfedgeMesh.SignedHeatMethod.cppm`.
- No dependency on geometry-central or libigl in production code paths (parity comparison only, in tests, behind an opt-in flag).

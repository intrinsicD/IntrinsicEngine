# GEOM-013 — Feature-preserving dual contouring

## Goal
- Add a feature-preserving dual contouring (DC) module that extracts a manifold triangle (or polygon) mesh from a signed distance field with sharp features preserved at edges where the gradient is discontinuous. Complements existing `Geometry.MarchingCubes` (which smooths over sharp features).

## Non-goals
- No replacement of `Geometry.MarchingCubes`; both algorithms coexist.
- No neural / learned variants as the default (may be added later behind a capability flag).
- No general iso-surface remeshing pipeline.
- No GPU backend.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #7.
- Closes the P1 "marching tetrahedra / dual contouring" gap from `docs/reviews/2026-05-12-src-geometry-gap-analysis.md`.
- Reuses `Geometry.Grid` (sampling), `Geometry.SDF` (oracle), and the dense small-matrix utilities from [`GEOM-008`](GEOM-008-linear-algebra-solver-infrastructure.md).
- **Hard dependency:** [`GEOM-015`](GEOM-015-common-method-package-infrastructure.md) for `Geometry::QEFSolver` (per-cell vertex placement), `Geometry::ClosestPointOracle` (`OracleFromSDF` adapter), `Geometry::Diagnostics`.

## Shared infrastructure consumed / extracted

This task **consumes** (depends on):

- `Geometry::QEFSolver::QEF3` + `SolveQEF` — the per-cell vertex placement is delegated to the shared QEF solver. Do **not** redefine `QEF3` here.
- `Geometry::ClosestPointOracle::OracleFromSDF` — used as the gradient sampler (`gradient = normal at closest-point projection`).
- `Geometry::Diagnostics` — sharp-feature-cell count, manifold-split-cell count, QEF rank-deficient cell count.

This task **does not** introduce new shared types. Output may include polygons with > 3 sides; these get triangulated before populating the `HalfedgeMesh::Mesh` output. Document the polygon-fan triangulation rule in the module header.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become opt-in capabilities or follow-up tasks.

- [ ] **A — Classical Dual Contouring with QEF (Ju, Losasso, Schaefer, Warren; SIGGRAPH 2002).** Per-cell quadratic-error-function vertex placement using SDF + gradient samples. Mature, deterministic, no manifold guarantees on highly twisted topology. Recommended default for a baseline reference.
- [ ] **B — Manifold Dual Contouring (Schaefer, Ju, Warren; IEEE Vis 2007).** Adds explicit per-cell vertex splitting to guarantee a manifold output. Heavier code; required if downstream consumers (e.g. halfedge mesh) cannot accept non-manifold input.
- [ ] **C — Power Diagram Enhanced Adaptive Isosurface Extraction (arXiv:2506.09579, 2025).** Power-diagram-based adaptive vertex placement; the current SOTA for feature preservation without ML. Recommended if state-of-the-art quality is the priority.
- [ ] **D — Neural Dual Contouring (Chen et al., arXiv:2202.01999) / Self-Supervised Dual Contouring (arXiv:2405.18131).** Learned vertex placement; depends on ML stack.

Default recommendation: **B** (Manifold DC) — the engine's halfedge mesh requires manifold input, so the manifold guarantee is worth the extra code over classical DC. Variant C is a strong follow-up once a reference exists.

## Required changes

- [ ] Add module `Geometry.DualContouring` in `src/geometry/Geometry.DualContouring.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::DualContouring {
    struct SdfSampler {
      virtual double Value(glm::dvec3 x) const = 0;
      virtual glm::dvec3 Gradient(glm::dvec3 x) const = 0;  // analytic preferred; FD fallback ok
    };
    struct Input {
      const SdfSampler& field;
      AABB bounds;
      glm::uvec3 resolution;     // grid cells
    };
    struct Params {
      double sharp_feature_angle_threshold_deg = 30.0;
      bool guarantee_manifold = true;          // variant B path
      double qef_singular_value_threshold = 0.1;
    };
    struct Result {
      HalfedgeMesh::Mesh mesh;
      Diagnostics diagnostics;                  // count of sharp-feature cells, manifold-split cells, QEF rank-deficient cells
    };
    Core::Expected<Result> Extract(const Input&, const Params&);
  }
  ```
- [ ] Register module in `src/geometry/CMakeLists.txt`. Do **not** umbrella-export from `Geometry.cppm` initially.
- [ ] Add a free function `Geometry::DualContouring::SamplerFromSdfModule(const SDF&)` adapter.

### Implementation steps (variant B reference path)
- [ ] Step 1: sample the grid corner SDF values; classify each grid edge as sign-changing if the two endpoint signs differ.
- [ ] Step 2: for each sign-changing edge, locate the iso-crossing point by linear interpolation of the SDF along the edge, and sample the gradient (oracle-normal) **at that crossing point**, not at the cell centre or grid corners. Each sign-changing edge contributes one `(point, normal)` pair to the per-cell `QEF3`.
- [ ] Step 3: per cell, solve the QEF via `Geometry::QEFSolver::SolveQEF` with `singular_value_threshold = Params::qef_singular_value_threshold` and `fallback_point = mean of edge crossings` (clamp to cell bounds).
- [ ] Step 4: connect dual vertices across cells sharing sign-changing edges. Each sign-changing edge is shared by exactly four cells; emit a quad (or triangulated quad fan) with those four dual vertices.
- [ ] Step 5: **manifold pass** (Schaefer-Ju-Warren 2007) — for each cell whose dual vertex would create a non-manifold edge (detected by analysing the local sign-pattern topology), split the cell into multiple dual vertices, one per disconnected component of the sign-pattern's interior. Implement the four-case split table from §3 of the 2007 paper. Without this pass, the output mesh cannot be loaded into `HalfedgeMesh::Mesh` (which is manifold-only).
- [ ] Step 6: triangulate any non-triangular faces (polygon fan from the centroid) before populating the `HalfedgeMesh::Mesh` output.

## Tests
- [ ] `tests/unit/geometry/Test.DualContouring.cpp` (CTest labels `unit;geometry`).
- [ ] Smooth sphere SDF — extracted mesh approximates sphere; mean distance < `h`.
- [ ] Box SDF — sharp edges/corners preserved; angles at corners within tolerance of 90°.
- [ ] Twisted SDF that produces non-manifold output under variant A — variant B must produce manifold output (verify with `Geometry.HalfedgeMesh.Analysis`).
- [ ] Determinism: same `(seed, resolution, bounds, field)` → bitwise identical output.

## Docs
- [ ] Add a short note in `docs/architecture/geometry.md` explaining MC vs DC trade-offs.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Document QEF singular-value clamping policy in `docs/methods/numerical-robustness-policy.md`.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Sphere + box tests pass with documented tolerances.
- [ ] Manifold check passes on twisted-topology fixture when variant B is enabled.
- [ ] No public Eigen types in `Geometry.DualContouring.cppm`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'DualContouring|MarchingCubes|SDF' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No replacement of `Geometry.MarchingCubes`.
- No neural variant as the default until a non-neural reference exists.
- No external ML framework dependency in production code.
- No coupling to `runtime` / `graphics` / `ecs` / `assets` / `platform`.

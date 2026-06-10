---
id: GEOM-013
theme: none
depends_on: []
---
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
- Reuses `Geometry.Grid` (sampling), `Geometry.SDF` (oracle), and the dense small-matrix utilities from [`GEOM-008`](../../done/GEOM-008-linear-algebra-solver-infrastructure.md) for the per-cell QEF solve.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become opt-in capabilities or follow-up tasks.

- [ ] **A — Classical Dual Contouring with QEF (Ju, Losasso, Schaefer, Warren; SIGGRAPH 2002).** Per-cell quadratic-error-function vertex placement using SDF + gradient samples. Mature, deterministic, no manifold guarantees on highly twisted topology. Recommended default for a baseline reference.
- [x] **B — Manifold Dual Contouring (Schaefer, Ju, Warren; IEEE Vis 2007).** Adds explicit per-cell vertex splitting to guarantee a manifold output. Heavier code; required if downstream consumers (e.g. halfedge mesh) cannot accept non-manifold input. **Selected as the default per the recommendation below.**
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
- [ ] Step 1: sample the grid; classify edges as sign-changing.
- [ ] Step 2: per cell, accumulate the QEF from sign-changing edges using gradient samples.
- [ ] Step 3: solve the per-cell QEF via SVD-with-singular-value-clamping (from `GEOM-008` dense utilities).
- [ ] Step 4: connect dual vertices across cells sharing sign-changing edges.
- [ ] Step 5: manifold pass — detect cells where dual vertices would create non-manifold edges and split them per Schaefer-Ju-Warren 2007.

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
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No replacement of `Geometry.MarchingCubes`.
- No neural variant as the default until a non-neural reference exists.
- No external ML framework dependency in production code.
- No coupling to `runtime` / `graphics` / `ecs` / `assets` / `platform`.

## Maturity
- Target: `CPUContracted` (pure CPU geometry contract under the default gate).
- No `Operational` follow-up is owed; this task has no backend seam.

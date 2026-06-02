# METHOD-007 — Constrained Delaunay tetrahedralization reference backend

## Goal
- Add a robust constrained Delaunay tetrahedralization (CDT) reference backend and a `Geometry.TetMesh` container, enabling volumetric meshing of closed triangle-mesh boundaries for FEM / soft-body simulation / volumetric remeshing.

## Non-goals
- No quality-driven tet refinement / sizing field in this task (follow-up).
- No hexahedral meshing.
- No GPU backend.
- No FEM solver assembly — this task ships only the container, the builder, and basic quality metrics.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/constrained_delaunay_tet/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #4.
- **Hard prerequisite:** [`GEOM-007`](../../done/GEOM-007-robust-predicates-intersection-classification.md) (robust 3D orientation + insphere predicates).
- Closes the P1 "volumetric and cell-complex containers" gap from `docs/reviews/2026-05-12-src-geometry-gap-analysis.md`.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become opt-in capabilities or follow-up tasks.

- [ ] **A — Constrained Delaunay Tetrahedrization: A Robust and Practical Approach (Diazzi, Panozzo, Jacobson, Attene; arXiv:2309.09805).** Achieves 100% success on the 4408 valid Thingi10k models. Recommended default — best robustness story without TetGen's GPL or fTetWild's heavier pipeline.
- [ ] **B — fTetWild (Hu, Schneider, Wang, Zorin, Panozzo; TOG 2020).** Floating-point tet-wild successor; very robust, produces high-quality tets, larger code surface and floating-point optimisation loop. Pick if mesh quality matters more than the build's strict reproducibility.
- [ ] **C — TetGen-style incremental CDT (Si, ACM TOMS 2015).** Mature classical algorithm. **License caveat:** TetGen is AGPL; only viable if the engine ships an independent re-implementation, not a wrapper.

Default recommendation: **A** (Diazzi et al.) — most recent, clean robustness guarantee, MIT-friendly reference code available.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/constrained_delaunay_tet/`.
- [ ] Fill `method.yaml` (`id: geometry.constrained_delaunay_tet`, metrics: `thingi10k_success_rate`, `min_dihedral_angle`, `max_aspect_ratio`, `runtime_ms`).
- [ ] Fill `paper.md` for the selected variant.

### Public API in `src/geometry`
- [ ] Add `Geometry.TetMesh` container in `src/geometry/Geometry.TetMesh.cppm` + `.cpp`. Mirror the property-set design used by `Geometry.HalfedgeMesh`:
  ```cpp
  namespace Geometry::TetMesh {
    struct VertexHandle { uint32_t id; };
    struct TetHandle    { uint32_t id; };
    class Mesh {
      // SoA: vertex positions, tet vertex indices, optional region tags.
      // Property sets: v:point, t:region, f:boundary, ...
    public:
      size_t VertexCount() const; size_t TetCount() const;
      std::span<const glm::dvec3> VertexPositions() const;
      std::span<const std::array<uint32_t,4>> TetVertices() const;
      // Boundary extraction returns a HalfedgeMesh::Mesh.
      HalfedgeMesh::Mesh ExtractBoundary() const;
    };
  }
  ```
- [ ] Add `Geometry.ConstrainedDelaunayTet` builder module in `src/geometry/Geometry.ConstrainedDelaunayTet.cppm` + `.cpp`:
  ```cpp
  namespace Geometry::ConstrainedDelaunayTet {
    struct Input { const HalfedgeMesh::Mesh& boundary; /* must be closed, watertight */ };
    struct Params { double target_edge_length = 0.0; /* 0 = no refinement */ };
    struct Result { TetMesh::Mesh mesh; Diagnostics diagnostics; };
    Core::Expected<Result> Build(const Input&, const Params&);
  }
  ```
- [ ] Add quality metrics module `Geometry.TetMesh.Quality` with dihedral-angle, aspect-ratio, inradius / circumradius ratio.
- [ ] Register modules in `src/geometry/CMakeLists.txt`; export `Geometry.TetMesh` from `Geometry.cppm` (container is a public type), keep the builder module out of the umbrella.

### Implementation (variant A reference path)
- [ ] Step 1: incremental Delaunay vertex insertion over the boundary vertices.
- [ ] Step 2: missing-edge / missing-face recovery via steiner-point insertion driven by robust insphere / orient3d (from `GEOM-007`).
- [ ] Step 3: in/out classification using boundary winding via existing `Geometry.BVH` + ray casting.
- [ ] Step 4: tag tets as `interior` / `exterior`; output only interior.

### Tetmesh IO
- [ ] Add VTK / MEDIT `.mesh` writer in `Geometry.TetMesh.IO`. Reader is optional in this task.

## Tests
- [ ] `tests/unit/geometry/Test.TetMesh.cpp` — container roundtrip, property set sanity, boundary extraction Euler check.
- [ ] `tests/unit/geometry/Test.ConstrainedDelaunayTet.cpp`.
- [ ] Unit cube boundary → tet mesh; verify volume equals 1.0 within tolerance; min dihedral > documented threshold.
- [ ] Sphere boundary → tet mesh; verify volume vs `4/3 π r³` within mesh-refinement error.
- [ ] Curated Thingi10k subset (≤ 10 closed manifolds) checked into `tests/data/cdt/`; document per-fixture success.
- [ ] Degenerate inputs (non-manifold edge, open surface, self-intersecting boundary) — verify diagnostics report the failure mode explicitly (no crashes, no asserts).

## Docs
- [ ] `methods/geometry/constrained_delaunay_tet/README.md`.
- [ ] Add `docs/architecture/geometry-volumetric.md` (new) introducing `Geometry.TetMesh` as the canonical volumetric container; link from `docs/architecture/geometry.md`.
- [ ] Update `tasks/backlog/geometry/README.md` to reference the new container.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Volume tests within tolerance for cube and sphere.
- [ ] Thingi10k subset success rate ≥ 95% on the curated fixtures.
- [ ] Diagnostics surface non-manifold / open / self-intersecting inputs without crashing.
- [ ] `GEOM-007` is closed before this task starts.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'TetMesh|ConstrainedDelaunay|RobustPredicates' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No TetGen / fTetWild as a runtime dependency — reference C/B variants must be re-implemented from the paper.
- No quality-driven refinement loop in this task (separate follow-up).
- No FEM solver coupling.
- No coupling to `runtime` / `graphics` / `ecs` / `assets` / `platform`.

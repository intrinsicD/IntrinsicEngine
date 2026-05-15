# METHOD-005 — Robust mesh boolean reference backend

## Goal
- Add a numerically robust mesh boolean kernel (union, intersection, difference, XOR) as a paper-method package, packaged alongside the existing `Geometry.HalfedgeMesh.Boolean` so the two can be compared during parity testing. After parity, the robust kernel becomes the default and the legacy module is reduced to a comparison baseline.

## Non-goals
- No GPU backend.
- No in-place rewrite of `Geometry.HalfedgeMesh.Boolean` during this task; both kernels must coexist until parity tests pass.
- No general arrangement / BSP toolkit beyond what the chosen variant requires.
- No CSG scripting layer / DSL.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/robust_boolean/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #5.
- **Hard prerequisites:**
  - [`GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md) — robust predicates + intersection classification (especially indirect predicates per Attene 2020 / Cherchi et al. 2022).
  - [`GEOM-015`](../geometry/GEOM-015-common-method-package-infrastructure.md) — `Geometry::Provenance`, `Geometry::Diagnostics`, `Geometry::Random` (for snap-rounding tie-breaks).
- The existing `Geometry.HalfedgeMesh.Boolean` produces wrong results on near-coincident and degenerate inputs from Thingi10k; this is the canonical robustness motivator.

## Shared infrastructure consumed / extracted

This task **consumes** (depends on):

- `Geometry::Provenance::OutputProvenance` (GEOM-015) — used as `Result::provenance`. The `ArrangementProvenance` placeholder in earlier sketches is replaced by this shared type; per-output triangle records its source mesh id + source triangle id + barycentric weights for attribute transfer.
- `Geometry::AttributeTransfer` helpers (GEOM-015) — handle vertex/face property interpolation onto the output.
- `Geometry::Diagnostics` — degenerate input counts, snap events, arrangement statistics.
- `Geometry::Random` — for deterministic tie-breaks during snap-rounding.

This task **may extract** (if not already in `GEOM-007`):

- BVH-vs-BVH overlap traversal — if the implementation produces a clean reusable routine, promote it as `Geometry::BVH::Overlap(const BVH&, const BVH&, OverlapVisitor&)` in `Geometry.BVH` rather than keeping it private to this method.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become optional capability flags or follow-up tasks.

- [ ] **A — Interactive and Robust Mesh Booleans (Cherchi, Livesu, Scateni, Attene; SIGGRAPH 2022, arXiv:2205.14151).** Hybrid floating-point + indirect predicates; ~200K-triangle inputs at interactive rates; public reference implementation. Recommended default for an engine-grade integration.
- [ ] **B — EMBER: Exact Mesh Booleans via Efficient & Robust Local Arrangements (Trettner, Reitmayr, Kobbelt; TOG 2022).** Plane-based representation with homogeneous integer coordinates; produces topologically exact output; heavier to integrate. Pick if "exact" output matters more than throughput.
- [ ] **C — Simple and Robust Boolean Operations for Triangulated Surfaces (Zhou, Grinspun, Zorin, Jacobson 2016).** Generalized-winding-number approach; simpler implementation; depends on `libigl`-style winding-number machinery. Pick only if implementation cost dominates and Thingi10k coverage is not a target.

Default recommendation: **A** (Cherchi et al.) — best balance of robustness, performance, and existing reference code to compare against.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/robust_boolean/`.
- [ ] Fill `method.yaml` (`id: geometry.robust_boolean`, metrics: `thingi10k_success_rate`, `output_triangle_count`, `runtime_ms`).
- [ ] Fill `paper.md` with full citation for the selected variant.

### Public API in `src/geometry`
- [ ] Add module `Geometry.HalfedgeMesh.RobustBoolean` in `src/geometry/Geometry.HalfedgeMesh.RobustBoolean.cppm` + `.cpp`.
- [ ] Public surface (sketch) — uses shared `Geometry::Provenance::OutputProvenance` and `Geometry::Diagnostics` from GEOM-015:
  ```cpp
  namespace Geometry::RobustBoolean {
    enum class Op { Union, Intersection, Difference, SymmetricDifference };
    struct Input {
      const HalfedgeMesh::Mesh& a;
      const HalfedgeMesh::Mesh& b;
      Op op;
    };
    struct Params {
      bool snap_round_inputs = true;
      double snap_epsilon = 1e-9;  // scale-relative
      bool transfer_attributes = true;
      uint64_t snap_seed = 0;      // tie-breaks (Geometry::Random)
    };
    struct Result {
      HalfedgeMesh::Mesh out;
      Geometry::Provenance::OutputProvenance provenance;  // shared type
      Geometry::Diagnostics diagnostics;                  // shared type
    };
    Core::Expected<Result> Compute(const Input&, const Params&);
  }
  ```
- [ ] Do **not** delete or rename `Geometry.HalfedgeMesh.Boolean` in this task; mark the legacy module as `[legacy]` in its module-level doc comment.

### Implementation steps (variant A reference path)
- [ ] Step 1: build per-mesh BVH using existing `Geometry.BVH`.
- [ ] Step 2: detect candidate intersecting triangle pairs via BVH-vs-BVH overlap.
- [ ] Step 3: compute exact / filtered triangle-triangle intersections via `GEOM-007` predicates.
- [ ] Step 4: local arrangement re-triangulation of each affected triangle.
- [ ] Step 5: in/out classification via ray casting or winding number on the merged arrangement.
- [ ] Step 6: select sub-arrangement per boolean op; stitch into output mesh.
- [ ] Step 7: provenance — record source mesh + source triangle id per output triangle for attribute transfer.

### Attribute transfer
- [ ] Transfer per-vertex and per-face properties via `Geometry.Properties` interpolation rules (barycentric for vertex, copy-from-source for face) using the provenance record.

## Tests
- [ ] `tests/unit/geometry/Test.RobustBoolean.cpp`.
- [ ] Smoke: two unit cubes (axis-aligned) — verify analytic volumes for union / intersection / difference.
- [ ] Coincident faces: two cubes sharing a face — both kernels should return well-defined output; legacy is expected to fail, robust kernel must succeed.
- [ ] Near-degenerate intersection: thin triangle pair from `tests/data/robust_boolean/` fixture set.
- [ ] Regression: a small curated subset of Thingi10k pairs (≤ 10 models) checked into `tests/data/robust_boolean/`; document success rate in `methods/geometry/robust_boolean/reports/`.
- [ ] Parity comparison test that runs both kernels on simple inputs and asserts agreement up to combinatorial reordering.

## Docs
- [ ] `methods/geometry/robust_boolean/README.md` — backend identity, default variant, known limitations.
- [ ] Update `methods/geometry/README.md` index.
- [ ] Add a note in `docs/architecture/geometry.md` explaining the dual-kernel coexistence and the planned legacy retirement task.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Robust kernel passes all unit cases the legacy kernel also passes (no regression).
- [ ] Robust kernel passes at least one case the legacy kernel fails on (motivation justified).
- [ ] Documented Thingi10k subset success rate ≥ 95% on the curated fixture set.
- [ ] `GEOM-007` is closed (or the prerequisite predicate API is already merged) before this task starts.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RobustBoolean|Boolean|RobustPredicates' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No deletion or rename of `Geometry.HalfedgeMesh.Boolean` in this task (separate retirement task once parity proven).
- No CGAL / libigl dependency in the production code path. Reference implementations may be used as **out-of-build** comparison tools in `methods/.../reports/` only.
- No reliance on `assert` for degeneracy handling — diagnostics must surface degeneracy as data.
- No GPU backend before reference parity.

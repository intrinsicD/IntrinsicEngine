# GEOM-014 — Feature-aware quadric error mesh simplification

## Goal
- Extend `Geometry.HalfedgeMesh.Simplification` with a feature-aware quadric error formulation that preserves sharp boundaries, normal discontinuities, and attribute discontinuities under aggressive decimation. Adds new error terms behind opt-in flags; the existing classical-QEM behaviour remains as the off-by-default fallback.

## Non-goals
- No replacement of `Geometry.HalfedgeMesh.Simplification` — this is an in-place extension.
- No GPU backend.
- No neural / learned simplification.
- No progressive-mesh / view-dependent LOD machinery in this task (separate follow-up).

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #8.
- Targets the existing module `Geometry.HalfedgeMesh.Simplification` (`src/geometry/Geometry.HalfedgeMesh.Simplification.cppm` + `.cpp`).
- Test target: `tests/unit/geometry/Test_Simplification.cpp` (existing; extend, do not replace).
- **Hard dependency:** [`GEOM-015`](GEOM-015-common-method-package-infrastructure.md) for `Geometry::QEFSolver` (vertex-placement quadratic), `Geometry::Diagnostics`.

## Shared infrastructure consumed / extracted

This task **consumes** (depends on):

- `Geometry::QEFSolver::QEF3` + `SolveQEF` — the per-collapse optimal-position computation uses the shared QEF solver, the same one used by `GEOM-013`. The classical QEM, FA-QEM, and intrinsic QEM variants all reduce to building different `QEF3` accumulators and feeding them to the same solver.
- `Geometry::Diagnostics` — collapse rejection counters, fallback-to-midpoint events.

This task **does not** introduce new shared types. The boundary-curvature term is computed as the discrete turning angle (`π - α` where `α` is the angle between adjacent boundary edges), accumulated per boundary loop; document this convention explicitly in the module header.

## Variants and default selection

Mark `[x]` next to the variant that should become the **default error metric** for new callers. Other metrics remain selectable via the `Params::metric` enum.

- [ ] **A — Classical QEM (Garland & Heckbert, SIGGRAPH 1997).** Plane-distance quadric only. Fastest, smallest code. Current behaviour.
- [ ] **B — FA-QEM: Feature-Aware QEM (Cao et al., arXiv:2605.14029, 2025).** Joint encoding of geometric deviation, boundary curvature, and surface normal consistency. Recommended new default for asset / LOD pipelines.
- [ ] **C — Surface Simplification Using Intrinsic Error Metrics (arXiv:2305.06410).** Intrinsic-Delaunay-aware error metric; pairs well with future intrinsic-triangulation work. Pick if intrinsic geometry is a year-1 priority.
- [ ] **D — Line-quadric variant (Hsueh-Ti Derek Liu et al., controlling QEM with line constraints).** Adds line-feature pinning. Useful for CAD / architectural assets.

Default recommendation: **B** (FA-QEM) — the survey's strongest "easy win," directly relevant to LOD / asset pipelines.

### Default-flip migration policy (resolves the existing-caller compatibility question)

This task ships the new metrics as **opt-in**. The default value of `Params::metric` does **not** change in this task — existing callers of `Simplify(mesh, Params{})` continue to receive classical-QEM behaviour. This satisfies the "no public API break" constraint.

A separate, narrower follow-up task (created during the next [`METHOD-008`](../methods/METHOD-008-recurring-method-sota-review.md) cycle after this one lands and benchmarks confirm parity) will:

1. Audit every consumer of `Simplify(...)` in the codebase.
2. Either set the metric explicitly per consumer, or accept the new default behaviour after explicit per-consumer review.
3. Flip the default value of `Params::metric` to the chosen variant in one mechanical commit.

Following the migration policy in [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md), at least one full review cycle (one quarter) must elapse between this task landing and the default flip.

## Required changes

- [ ] Extend the existing public surface in `src/geometry/Geometry.HalfedgeMesh.Simplification.cppm`:
  ```cpp
  namespace Geometry::HalfedgeMesh::Simplification {
    enum class Metric { ClassicalQEM, FA_QEM, IntrinsicQEM, LineQEM };
    struct Params {
      Metric metric = Metric::FA_QEM;        // <-- new default once landed
      double target_ratio = 0.5;              // fraction of original face count
      double boundary_weight = 1.0;
      double normal_weight = 1.0;             // FA-QEM only
      double curvature_weight = 1.0;          // FA-QEM only
      bool preserve_topology = true;
      bool preserve_uv_seams = true;
    };
    struct Diagnostics { uint32_t collapses_rejected_topology; uint32_t collapses_rejected_quality; /* ... */ };
    struct Result { HalfedgeMesh::Mesh mesh; Diagnostics diagnostics; };
    Core::Expected<Result> Simplify(const HalfedgeMesh::Mesh&, const Params&);
  }
  ```
- [ ] Implement FA-QEM error terms in `src/geometry/Geometry.HalfedgeMesh.Simplification.cpp`:
  - geometric quadric (existing path),
  - boundary-curvature term: integrate curvature along boundary loops adjacent to collapse,
  - normal-consistency term: penalty proportional to angle between adjacent face normals before / after collapse.
- [ ] Combine quadrics as a weighted sum; expose weights through `Params`.
- [ ] Keep the existing classical-QEM code path reachable via `Metric::ClassicalQEM` for parity comparison.
- [ ] Add `Geometry.HalfedgeMesh.Simplification.IntrinsicQEM.cpp` only if variant C is also marked as a follow-up sub-task.

## Tests
- [ ] Extend `tests/unit/geometry/Test_Simplification.cpp` with:
  - A cube fixture: aggressive simplification at 10% ratio must preserve 8 corner vertices when `Metric::FA_QEM`; classical QEM is permitted to collapse them.
  - A bunny / fertility fixture (existing): mean Hausdorff distance with FA-QEM must be ≤ classical-QEM Hausdorff at the same triangle-count target.
  - A textured fixture with UV seams: UV seams remain intact when `preserve_uv_seams = true`.
- [ ] Regression: deterministic output for fixed input and fixed params (no RNG involved).
- [ ] Diagnostics test: collapse-rejection counters non-zero for known-degenerate inputs.

## Docs
- [ ] Update the module's `paper.md`-equivalent comment block at the top of `Geometry.HalfedgeMesh.Simplification.cppm` with the new metric options and the default.
- [ ] Add a short benchmark report stub in `benchmarks/geometry/` comparing classical-QEM vs FA-QEM on the smoke fixtures (no performance claims, only quality metrics).
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Cube-corner test passes for the default metric.
- [ ] Hausdorff-distance test shows FA-QEM ≤ classical QEM at the documented target ratio.
- [ ] UV-seam preservation test passes.
- [ ] No public API break for existing callers — old `Params` field defaults preserve previous behaviour if `Metric::ClassicalQEM` is explicitly requested.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Simplification' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No rewrite of the existing classical-QEM code path; classical-QEM behaviour must remain reachable.
- No silent change of default semantics for existing callers — if `Metric` is unset, document the chosen default in the module header and update one consumer test at a time.
- No neural / learned variant in this task.
- No GPU backend.

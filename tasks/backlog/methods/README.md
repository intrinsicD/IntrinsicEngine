# Methods Backlog

Paper/method packages following
[`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
paper intake → CPU reference → correctness tests → benchmark harness →
optimized CPU → GPU only after reference parity.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Program tasks

- [METHODS-001 — Pin signed heat as methods-pipeline pathfinder](METHODS-001-signed-heat-pathfinder.md).

## Tasks

- [METHOD-001 — Rigid-body dynamics reference backend](METHOD-001-rigid-body-dynamics-reference-backend.md)
  (ownership gate accepted by [`ARCH-001`](../../done/ARCH-001-physics-layer-ownership-and-ecs-integration.md)
  / [ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md);
  ECS authoring side handled by retired
  [`HARDEN-064`](../../done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)).
- [METHOD-002 — Signed Heat Method reference backend](METHOD-002-signed-heat-method-reference-backend.md)
  **(pathfinder method per [METHODS-001](METHODS-001-signed-heat-pathfinder.md))**.
- [METHOD-003 — Closest Point Method PDE solver reference backend](METHOD-003-closest-point-method-pde-reference-backend.md).
- [METHOD-004 — Walk on Spheres / Walk on Stars PDE solver reference backend](METHOD-004-walk-on-spheres-reference-backend.md).
- [METHOD-005 — Robust mesh boolean reference backend](METHOD-005-robust-mesh-boolean-reference-backend.md)
  (hard-gated by [`geometry/GEOM-007`](../../done/GEOM-007-robust-predicates-intersection-classification.md)).
- [METHOD-006 — Cross-field / frame-field design reference backend](METHOD-006-cross-field-design-reference-backend.md).
- [METHOD-007 — Constrained Delaunay tetrahedralization reference backend](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
  (hard-gated by [`geometry/GEOM-007`](../../done/GEOM-007-robust-predicates-intersection-classification.md)).
- [METHOD-008 — Resolve `_example_vector_heat` method manifest placeholders](METHOD-008-example-vector-heat-manifest-placeholders.md)
  (metadata/intake cleanup; no CPU reference backend in this task).

## Convergence

- METHOD-001 contributes to **Theme C — Physics readiness**. The CPU reference
  package may proceed under accepted ADR-0019, but runtime/ECS integration and
  any performance backend remain out of scope for the method package and must
  wait for the ECS authoring and physics/runtime bridge tasks.
- METHOD-002 through METHOD-007 are seeded by the geometry paper survey
  [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md)
  and target gaps from
  [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
  Each task lists explicit algorithm variants and requires the maintainer to mark
  one as the public-facing default backend before implementation begins.
- Ordering: [`geometry/GEOM-008`](../../done/GEOM-008-linear-algebra-solver-infrastructure.md)
  retired 2026-05-27 at `CPUContracted` shipping the CSR builder + CG /
  shifted-CG iterative solver. The direct sparse SPD factorization
  (LDLT/LLT) path that METHOD-002 (step 2) and METHOD-003 (step 5)
  name is owed by the follow-up
  [`geometry/GEOM-020`](../geometry/GEOM-020-sparse-direct-factorization-seam.md);
  METHOD-002 and METHOD-003 must wait on `GEOM-020`, not on retired
  `GEOM-008`. METHOD-004 needs no LDLT path and may proceed against
  retired `GEOM-008` directly. METHOD-005 and METHOD-007 both wait on
  [`geometry/GEOM-007`](../../done/GEOM-007-robust-predicates-intersection-classification.md).
  METHOD-006 step 4 needs a sparse symmetric generalized eigensolver
  (LOBPCG / shift-invert) which is shipped by neither GEOM-008 nor
  GEOM-020; a separate eigensolver follow-up must be filed before
  METHOD-006 can promote on variant B.
- Forbidden: importing runtime, graphics, platform, app, or live ECS ownership
  into a method package; claiming performance wins without a baseline.

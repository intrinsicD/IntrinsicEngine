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
  (gated by [`physics/ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md);
  ECS authoring side handled by [`ecs/HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)).
- [METHOD-002 — Signed Heat Method reference backend](METHOD-002-signed-heat-method-reference-backend.md)
  **(pathfinder method per [METHODS-001](METHODS-001-signed-heat-pathfinder.md))**.
- [METHOD-003 — Closest Point Method PDE solver reference backend](METHOD-003-closest-point-method-pde-reference-backend.md).
- [METHOD-004 — Walk on Spheres / Walk on Stars PDE solver reference backend](METHOD-004-walk-on-spheres-reference-backend.md).
- [METHOD-005 — Robust mesh boolean reference backend](METHOD-005-robust-mesh-boolean-reference-backend.md)
  (hard-gated by [`geometry/GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md)).
- [METHOD-006 — Cross-field / frame-field design reference backend](METHOD-006-cross-field-design-reference-backend.md).
- [METHOD-007 — Constrained Delaunay tetrahedralization reference backend](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
  (hard-gated by [`geometry/GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md)).

## Convergence

- METHOD-001 contributes to **Theme C — Physics readiness**. The CPU reference
  package may be drafted independently, but runtime/ECS integration and any
  performance backend must wait for the physics layer ownership decision in
  ARCH-001.
- METHOD-002 through METHOD-007 are seeded by the geometry paper survey
  [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md)
  and target gaps from
  [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
  Each task lists explicit algorithm variants and requires the maintainer to mark
  one as the public-facing default backend before implementation begins.
- Ordering: METHOD-002, METHOD-003, METHOD-004 can proceed in parallel once
  [`geometry/GEOM-008`](../geometry/GEOM-008-linear-algebra-solver-infrastructure.md)
  exposes the sparse-solver seam. METHOD-005 and METHOD-007 both wait on
  [`geometry/GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md).
  METHOD-006 has no hard gate beyond `GEOM-008`.
- Forbidden: importing runtime, graphics, platform, app, or live ECS ownership
  into a method package; claiming performance wins without a baseline.

# Methods Backlog

Paper/method packages following
[`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
paper intake → CPU reference → correctness tests → benchmark harness →
optimized CPU → GPU only after reference parity.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [METHOD-001 — Rigid-body dynamics reference backend](METHOD-001-rigid-body-dynamics-reference-backend.md)
  (gated by [`physics/ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md);
  ECS authoring side handled by [`ecs/HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)).
- [METHOD-002 — Signed Heat Method reference backend](METHOD-002-signed-heat-method-reference-backend.md).
- [METHOD-003 — Closest Point Method PDE solver reference backend](METHOD-003-closest-point-method-pde-reference-backend.md).
- [METHOD-004 — Walk on Spheres / Walk on Stars PDE solver reference backend](METHOD-004-walk-on-spheres-reference-backend.md).
- [METHOD-005 — Robust mesh boolean reference backend](METHOD-005-robust-mesh-boolean-reference-backend.md)
  (hard-gated by [`geometry/GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md)).
- [METHOD-006 — Cross-field / frame-field design reference backend](METHOD-006-cross-field-design-reference-backend.md).
- [METHOD-007 — Constrained Delaunay tetrahedralization reference backend](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
  (hard-gated by [`geometry/GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md)).
- [METHOD-008 — Recurring method SOTA review (standing task)](METHOD-008-recurring-method-sota-review.md)
  (process: [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md)).

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
- Ordering: every implementation method task (METHOD-002..007, plus
  [`geometry/GEOM-013`](../geometry/GEOM-013-feature-preserving-dual-contouring.md)
  and [`geometry/GEOM-014`](../geometry/GEOM-014-feature-aware-quadric-error-simplification.md))
  hard-depends on
  [`geometry/GEOM-015`](../geometry/GEOM-015-common-method-package-infrastructure.md)
  for shared `Diagnostics`, `Random`, `ClosestPointOracle`, `Connection`,
  `BoundaryConditions`, `Provenance`, and `QEFSolver` types. Land GEOM-015 first.
  After that: METHOD-002, METHOD-003, METHOD-004, METHOD-006 can proceed in
  parallel once [`geometry/GEOM-008`](../geometry/GEOM-008-linear-algebra-solver-infrastructure.md)
  exposes the sparse-solver seam. METHOD-005 and METHOD-007 also need
  [`geometry/GEOM-007`](../geometry/GEOM-007-robust-predicates-intersection-classification.md).
- METHOD-008 is the **standing recurring task** for quarterly SOTA review of
  every method package; it never moves to `tasks/done/`. Its process doc is
  [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md).
  Each cycle produces a dated review note under `docs/reviews/` and any required
  follow-up implementation tasks.
- Forbidden: importing runtime, graphics, platform, app, or live ECS ownership
  into a method package; claiming performance wins without a baseline.

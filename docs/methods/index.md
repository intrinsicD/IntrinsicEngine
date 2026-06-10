# Methods Documentation

This index is the canonical entry point for method/paper implementation documentation.

> **Pathfinder method.** [`METHOD-002 — Signed Heat Method reference backend`](../../tasks/backlog/methods/METHOD-002-signed-heat-method-reference-backend.md) is pinned as the first method to be driven end-to-end through the methods pipeline (paper intake → CPU reference → correctness tests → benchmark harness → docs). See [`METHODS-001`](../../tasks/backlog/methods/METHODS-001-signed-heat-pathfinder.md) for the rationale and dependency chain. Treat METHOD-002's resulting `methods/geometry/signed_heat/` package as the canonical pattern when authoring future method packages.

## Start here

- [Method template](method-template.md)
- [Method manifest schema](method-manifest-schema.md)
- [Reference implementation policy](reference-implementation-policy.md)
- [Backend policy](backend-policy.md)
- [Numerical robustness policy](numerical-robustness-policy.md)
- [Dataset policy](dataset-policy.md)
- [Report template](report-template.md)

## Related docs

- [Agent method workflow](../agent/method-workflow.md)
- [Methods directory overview](../../methods/README.md)

## Physics Methods

- [`physics.rigid_body_reference`](../../methods/physics/rigid_body_reference/)
  is the deterministic CPU reference backend for fixed-step rigid-body
  integration and first-phase analytic contact contracts. It retires
  [`METHOD-001`](../../tasks/done/METHOD-001-rigid-body-dynamics-reference-backend.md)
  at `CPUContracted`; CPU-only runtime/world integration is now
  `CPUContracted` by
  [`PHYSICS-001`](../../tasks/done/PHYSICS-001-physics-world-state-and-runtime-sync.md).
- [`methods/physics/README.md`](../../methods/physics/) records the physics
  method roadmap from
  [`ARCH-002`](../../tasks/done/ARCH-002-physics-phenomena-roadmap.md). The
  first non-rigid follow-ups are
  [`METHOD-009`](../../tasks/done/METHOD-009-particle-spring-reference-backend.md),
  [`METHOD-010`](../../tasks/backlog/methods/METHOD-010-xpbd-cloth-shell-reference-backend.md),
  and [`METHOD-011`](../../tasks/backlog/methods/METHOD-011-sph-fluid-reference-backend.md).
  Each remains CPU-reference-first; GPU backends require later parity-proven
  tasks.

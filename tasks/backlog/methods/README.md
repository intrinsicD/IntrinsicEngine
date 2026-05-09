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

## Convergence

- METHOD-001 contributes to **Theme C — Physics readiness**. The CPU reference
  package may be drafted independently, but runtime/ECS integration and any
  performance backend must wait for the physics layer ownership decision in
  ARCH-001.
- Forbidden: importing runtime, graphics, platform, app, or live ECS ownership
  into a method package; claiming performance wins without a baseline.

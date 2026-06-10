# Physics Backlog

Physics layer ownership, runtime-independent physics-world implementation, and
phenomena roadmap. `src/physics` is approved by
[ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md)
as a promoted layer with `physics -> core, geometry` dependencies. `PHYSICS-001`
has added the first CPU-only world/state source and runtime bridge.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [ARCH-001 - Define physics layer ownership and ECS integration](../../done/ARCH-001-physics-layer-ownership-and-ecs-integration.md) (done 2026-06-05).
- [ARCH-002 - Physics phenomena roadmap and method selection](../../done/ARCH-002-physics-phenomena-roadmap.md) (done 2026-06-06).
- [PHYSICS-001 - Physics world state and runtime fixed-step sync](../../done/PHYSICS-001-physics-world-state-and-runtime-sync.md) (done 2026-06-05).
- [PHYSICS-002 - Collision broadphase/narrowphase contract](../../done/PHYSICS-002-collision-broadphase-narrowphase-contract.md) (done 2026-06-06).
- [PHYSICS-003 - Constraints, islands, sleep, and solver diagnostics](../../done/PHYSICS-003-constraints-islands-and-solver-diagnostics.md) (done 2026-06-10).

## Convergence

- These tasks anchor **Theme C - Physics readiness**.
- ARCH-001 is accepted, the ECS authoring contract
  [`HARDEN-064`](../../done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)
  is retired, and the rigid-body reference method
  [`METHOD-001`](../../done/METHOD-001-rigid-body-dynamics-reference-backend.md)
  is retired at `CPUContracted`.
- `PHYSICS-001` is retired at `CPUContracted`; it owns the first
  `src/physics` world/state source addition and `Extrinsic.Runtime.PhysicsBridge`.
- `PHYSICS-002` is retired at `CPUContracted`; it owns deterministic CPU
  broadphase candidates, first-phase primitive contact records, and collision
  diagnostics.
- `PHYSICS-003` is now unblocked by retired `PHYSICS-001` and `PHYSICS-002`; it
  owns constraint, island, sleep, and solver diagnostics.
- ARCH-002 must not bless GPU/optimized backend tasks for any phenomenon before
  its CPU reference path exists. It retired with first non-rigid physics method
  follow-ups:
  [`METHOD-009`](../../done/METHOD-009-particle-spring-reference-backend.md) (done),
  [`METHOD-010`](../../done/METHOD-010-xpbd-cloth-shell-reference-backend.md) (done),
  and [`METHOD-011`](../methods/METHOD-011-sph-fluid-reference-backend.md).

Forbidden across all members: physics solver code in ECS, runtime-owned solver
internals, graphics/RHI dependencies in physics, or treating rendering
visualization as physics correctness evidence.

# Physics Backlog

Physics layer ownership, first runtime-independent physics-world implementation,
and phenomena roadmap. `src/physics` is approved by
[ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md)
as a promoted layer with `physics -> core, geometry` dependencies, but no
physics source code exists yet.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [ARCH-001 - Define physics layer ownership and ECS integration](../../done/ARCH-001-physics-layer-ownership-and-ecs-integration.md) (done 2026-06-05).
- [ARCH-002 - Physics phenomena roadmap and method selection](ARCH-002-physics-phenomena-roadmap.md).
- [PHYSICS-001 - Physics world state and runtime fixed-step sync](PHYSICS-001-physics-world-state-and-runtime-sync.md).
- [PHYSICS-002 - Collision broadphase/narrowphase contract](PHYSICS-002-collision-broadphase-narrowphase-contract.md).
- [PHYSICS-003 - Constraints, islands, sleep, and solver diagnostics](PHYSICS-003-constraints-islands-and-solver-diagnostics.md).

## Convergence

- These tasks anchor **Theme C - Physics readiness**.
- ARCH-001 is accepted and unblocks the ECS authoring contract
  [`ecs/HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)
  plus the rigid-body reference method
  [`methods/METHOD-001`](../methods/METHOD-001-rigid-body-dynamics-reference-backend.md).
- `PHYSICS-001` should wait for `HARDEN-064` and enough `METHOD-001` reference
  coverage to validate deterministic stepping. It owns the first `src/physics`
  world/state source addition and runtime bridge.
- `PHYSICS-002` depends on `PHYSICS-001` and owns collision broadphase/
  narrowphase contracts.
- `PHYSICS-003` depends on `PHYSICS-001` and `PHYSICS-002` and owns constraint,
  island, sleep, and solver diagnostics.
- ARCH-002 must not bless GPU/optimized backend tasks for any phenomenon before
  its CPU reference path exists.

Forbidden across all members: physics solver code in ECS, runtime-owned solver
internals, graphics/RHI dependencies in physics, or treating rendering
visualization as physics correctness evidence.

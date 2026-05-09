# Physics Backlog

Physics layer ownership decisions and phenomena roadmap. `src/physics` does
not currently exist; the `AGENTS.md` source-tree map does not include a
`physics` layer, so any source-layout addition requires the layer-ownership
ADR ([ARCH-001](ARCH-001-physics-layer-ownership-and-ecs-integration.md)) to
be accepted first and reflected in `AGENTS.md` plus
`docs/architecture/index.md`.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [ARCH-001 — Define physics layer ownership and ECS integration](ARCH-001-physics-layer-ownership-and-ecs-integration.md).
- [ARCH-002 — Physics phenomena roadmap and method selection](ARCH-002-physics-phenomena-roadmap.md).

## Convergence

- These tasks anchor **Theme C — Physics readiness**.
- ARCH-001 is the upstream gate for
  [`methods/METHOD-001`](../methods/METHOD-001-rigid-body-dynamics-reference-backend.md)
  and [`ecs/HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md).
  No physics solver code, runtime sync, or `src/physics/*` source addition
  lands before ARCH-001 is accepted.
- ARCH-002 must not bless GPU/optimized backend tasks for any phenomenon
  before its CPU reference path exists.

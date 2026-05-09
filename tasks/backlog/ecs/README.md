# ECS Backlog

Promoted ECS layer hardening: scene bootstrap, hierarchy/transform parity,
layer boundary cleanup, event seams, and authoring contracts. `ecs -> core`
only; geometry handles/types are allowed when explicitly required, and
graphics/runtime/platform/app imports are forbidden.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [HARDEN-060 — Promote ECS scene bootstrap contract](../../done/HARDEN-060-ecs-scene-bootstrap-contract.md) (done).
- [HARDEN-061 — Promote ECS hierarchy and transform system parity](../../done/HARDEN-061-ecs-hierarchy-transform-system-parity.md) (done).
- [HARDEN-062 — Harden ECS layering and component boundaries](../../active/HARDEN-062-ecs-layering-and-component-boundary-hardening.md) (active).
- [HARDEN-063 — Define promoted ECS event and command seams](HARDEN-063-ecs-events-and-command-seams.md).
- [HARDEN-064 — Define ECS collider and rigid-body authoring contracts](HARDEN-064-ecs-collider-rigidbody-authoring-contract.md).

## Convergence

- HARDEN-060..063 contribute to **Theme D — ECS hardening parity**.
- HARDEN-060..062 are required prerequisites for **Theme A — Shortest path to
  sandbox visible geometry** (renderable extraction needs promoted scene
  bootstrap and hierarchy/transform behavior).
- HARDEN-064 is in **Theme C — Physics readiness** and is gated by
  [`physics/ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md);
  it must not start before that decision lands.
- Forbidden across all members: physics-world handles, runtime sidecars,
  graphics handles, RHI handles, or live `AssetService` traffic in canonical
  ECS components.
